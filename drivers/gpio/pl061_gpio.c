// SPDX-License-Identifier: GPL-2.0
/*
 * Arm PL061 GPIO driver
 *
 * QEMU 'virt' and various Arm SoCs.
 */

#include <dm.h>
#include <errno.h>
#include <asm/gpio.h>
#include <asm/io.h>
#include <linux/bitops.h>

#define PL061_DIR	0x400

#define PL061_NR_GPIOS	8

struct pl061_gpio_priv {
	void __iomem *base;
};

/*
 * GPIODATA uses masked addressing: bits [9:2] of the register offset are the
 * mask of the pins the access applies to, so a single pin is read or written
 * at base + (BIT(pin) << 2).
 */
static void __iomem *pl061_data_reg(void __iomem *base, unsigned int offset)
{
	return base + (BIT(offset) << 2);
}

static int pl061_gpio_get_value(struct udevice *dev, unsigned int offset)
{
	struct pl061_gpio_priv *priv = dev_get_priv(dev);

	return !!readl(pl061_data_reg(priv->base, offset));
}

static int pl061_gpio_set_value(struct udevice *dev, unsigned int offset,
				int value)
{
	struct pl061_gpio_priv *priv = dev_get_priv(dev);

	writel(value ? BIT(offset) : 0, pl061_data_reg(priv->base, offset));

	return 0;
}

static int pl061_gpio_direction_input(struct udevice *dev, unsigned int offset)
{
	struct pl061_gpio_priv *priv = dev_get_priv(dev);

	clrbits_le32(priv->base + PL061_DIR, BIT(offset));

	return 0;
}

static int pl061_gpio_direction_output(struct udevice *dev, unsigned int offset,
				       int value)
{
	struct pl061_gpio_priv *priv = dev_get_priv(dev);

	setbits_le32(priv->base + PL061_DIR, BIT(offset));
	writel(value ? BIT(offset) : 0, pl061_data_reg(priv->base, offset));

	return 0;
}

static int pl061_gpio_get_function(struct udevice *dev, unsigned int offset)
{
	struct pl061_gpio_priv *priv = dev_get_priv(dev);

	return readl(priv->base + PL061_DIR) & BIT(offset) ?
		GPIOF_OUTPUT : GPIOF_INPUT;
}

static const struct dm_gpio_ops pl061_gpio_ops = {
	.direction_input	= pl061_gpio_direction_input,
	.direction_output	= pl061_gpio_direction_output,
	.get_value		= pl061_gpio_get_value,
	.set_value		= pl061_gpio_set_value,
	.get_function		= pl061_gpio_get_function,
};

static int pl061_gpio_probe(struct udevice *dev)
{
	struct gpio_dev_priv *uc_priv = dev_get_uclass_priv(dev);
	struct pl061_gpio_priv *priv = dev_get_priv(dev);

	priv->base = dev_read_addr_ptr(dev);
	if (!priv->base)
		return -EINVAL;

	uc_priv->bank_name = dev->name;
	uc_priv->gpio_count = PL061_NR_GPIOS;

	return 0;
}

static const struct udevice_id pl061_gpio_ids[] = {
	{ .compatible = "arm,pl061" },
	{ }
};

U_BOOT_DRIVER(pl061_gpio) = {
	.name		= "pl061_gpio",
	.id		= UCLASS_GPIO,
	.of_match	= pl061_gpio_ids,
	.priv_auto	= sizeof(struct pl061_gpio_priv),
	.ops		= &pl061_gpio_ops,
	.probe		= pl061_gpio_probe,
};
