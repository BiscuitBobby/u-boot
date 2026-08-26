// SPDX-License-Identifier: GPL-2.0+
/*
 * (C) Copyright 2023 Dzmitry Sankouski <dsankouski@gmail.com>
 */

#include <stdlib.h>
#include <dm.h>
#include <fdtdec.h>
#include <input.h>
#include <keyboard.h>
#include <button.h>
#include <dm/device-internal.h>
#include <log.h>
#include <asm/io.h>
#include <asm/gpio.h>
#include <linux/bitops.h>
#include <linux/delay.h>
#include <linux/input.h>

/* The key that acts as a modifier while it is held down */
#define BUTTON_CHORD_MODIFIER	KEY_POWER

#define BUTTON_CHORD_MAX	((int)BITS_PER_TYPE(u32))

/**
 * struct button_kbd_priv - driver private data
 *
 * @input: input configuration
 * @button_size: number of buttons found
 * @old_state: a pointer to old button states array. Used to determine button state change.
 * @chord_down: bitmask of buttons remapped by the modifier
 * @mod_held: the modifier key is down
 * @mod_used: the modifier has remapped a key while down
 */
struct button_kbd_priv {
	struct input_config *input;
	u32 button_size;
	u32 *old_state;
	u32 chord_down;
	bool mod_held;
	bool mod_used;
};

static const struct {
	int raw;	/* keycode as the button reports it */
	int chorded;	/* what it becomes while the modifier is held */
} chord_map[] = {
	{ KEY_VOLUMEUP,   KEY_KPPLUS  },
	{ KEY_VOLUMEDOWN, KEY_KPMINUS },
};

static int button_chord_code(int raw)
{
	int i;
	/* Return the chorded keycode for @raw, or 0 */
	for (i = 0; i < ARRAY_SIZE(chord_map); i++)
		if (chord_map[i].raw == raw)
			return chord_map[i].chorded;

	return 0;
}

static int button_kbd_start(struct udevice *dev)
{
	struct button_kbd_priv *priv = dev_get_priv(dev);
	int i = 0;
	struct udevice *button_gpio_devp, *next_devp;
	struct uclass *uc;

	uclass_foreach_dev_probe(UCLASS_BUTTON, button_gpio_devp) {
		struct button_uc_plat *uc_plat = dev_get_uclass_plat(button_gpio_devp);
		/* Ignore the top-level button node */
		if (!uc_plat->label)
			continue;
		debug("Found button %s #%d - %s, probing...\n",
		      uc_plat->label, i, button_gpio_devp->name);
		i++;
	}

	if (uclass_get(UCLASS_BUTTON, &uc))
		return -ENOENT;

	/*
	 * Unbind any buttons that failed to probe so we don't iterate over
	 * them when polling.
	 */
	uclass_foreach_dev_safe(button_gpio_devp, next_devp, uc) {
		if (!(dev_get_flags(button_gpio_devp) & DM_FLAG_ACTIVATED)) {
			log_warning("Button %s failed to probe\n",
				    button_gpio_devp->name);
			device_unbind(button_gpio_devp);
		}
	}

	priv->button_size = i;
	priv->old_state = calloc(i, sizeof(int));

	return 0;
}

int button_read_keys(struct input_config *input)
{
	struct button_kbd_priv *priv = dev_get_priv(input->dev);
	struct udevice *button_gpio_devp;
	struct uclass *uc;
	int i, idx = 0;
	u32 code, state, state_changed = 0;

	uclass_id_foreach_dev(UCLASS_BUTTON, button_gpio_devp, uc) {
		struct button_uc_plat *uc_plat = dev_get_uclass_plat(button_gpio_devp);
		/* Ignore the top-level button node */
		if (!uc_plat->label)
			continue;
		code = button_get_code(button_gpio_devp);
		if (!code)
			continue;

		i = idx++;
		state = button_get_state(button_gpio_devp) == BUTTON_ON;
		state_changed = state != priv->old_state[i];
		priv->old_state[i] = state;

		if (CONFIG_IS_ENABLED(BUTTON_REMAP_PHONE_KEYS)) {
			int raw = button_get_ops(button_gpio_devp)->get_code(button_gpio_devp);
			int chorded_code;

			if (raw == BUTTON_CHORD_MODIFIER) {
				priv->mod_held = state;
				if (state_changed) {
					if (state) {
						priv->mod_used = false;
					} else if (!priv->mod_used) {
						input_add_keycode(input, code, false);
						input_add_keycode(input, code, true);
					}
				}
				continue;
			}

			chorded_code = button_chord_code(raw);
			if (chorded_code) {
				u32 bit = i < BUTTON_CHORD_MAX ? BIT(i) : 0;

				if (state_changed && state && priv->mod_held)
					priv->chord_down |= bit;
				if (priv->chord_down & bit) {
					code = chorded_code;
					priv->mod_used = true;
					if (state_changed && !state)
						priv->chord_down &= ~bit;
				}
			}
		}

		if (state_changed) {
			debug("%s: %d\n", uc_plat->label, code);
			input_add_keycode(input, code, !state);
		}
	}
	return 0;
}

static const struct keyboard_ops button_kbd_ops = {
	.start	= button_kbd_start,
};

static int button_kbd_probe(struct udevice *dev)
{
	struct button_kbd_priv *priv = dev_get_priv(dev);
	struct keyboard_priv *uc_priv = dev_get_uclass_priv(dev);
	struct stdio_dev *sdev = &uc_priv->sdev;
	struct input_config *input = &uc_priv->input;
	int ret = 0;

	input_init(input, false);
	input_add_tables(input, false);

	/* Register the device. */
	priv->input = input;
	input->dev = dev;
	input->read_keys = button_read_keys;
	strcpy(sdev->name, "button-kbd");
	ret = input_stdio_register(sdev);
	if (ret) {
		debug("%s: input_stdio_register() failed\n", __func__);
		return ret;
	}

	return 0;
}

U_BOOT_DRIVER(button_kbd) = {
	.name		= "button_kbd",
	.id		= UCLASS_KEYBOARD,
	.ops		= &button_kbd_ops,
	.priv_auto	= sizeof(struct button_kbd_priv),
	.probe		= button_kbd_probe,
};

U_BOOT_DRVINFO(button_kbd) = {
	.name = "button_kbd"
};
