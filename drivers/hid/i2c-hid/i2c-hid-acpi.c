/*
 * HID over I2C ACPI Subclass
 *
 * Copyright (c) 2012 Benjamin Tissoires <benjamin.tissoires@gmail.com>
 * Copyright (c) 2012 Ecole Nationale de l'Aviation Civile, France
 * Copyright (c) 2012 Red Hat, Inc
 *
 * This code was forked out of the core code, which was partly based on
 * "USB HID support for Linux":
 *
 *  Copyright (c) 1999 Andreas Gal
 *  Copyright (c) 2000-2005 Vojtech Pavlik <vojtech@suse.cz>
 *  Copyright (c) 2005 Michael Haboustak <mike-@cinci.rr.com> for Concept2, Inc
 *  Copyright (c) 2007-2008 Oliver Neukum
 *  Copyright (c) 2006-2010 Jiri Kosina
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive for
 * more details.
 */

#include <linux/acpi.h>
#include <linux/device.h>
#include <linux/i2c.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pm.h>

#include "i2c-hid.h"

struct i2c_hid_acpi {
	struct i2chid_subclass_data subclass;
	struct i2c_client *client;
	bool power_fixed;
};

static const struct acpi_device_id i2c_hid_acpi_blacklist[] = {
	/*
	 * The CHPN0001 ACPI device, which is used to describe the Chipone
	 * ICN8505 controller, has a _CID of PNP0C50 but is not HID compatible.
	 */
	{"CHPN0001", 0 },
	{ },
};

static int i2c_hid_acpi_get_descriptor(struct i2c_client *client)
{
	static guid_t i2c_hid_guid =
		GUID_INIT(0x3CDFF6F7, 0x4267, 0x4555,
			  0xAD, 0x05, 0xB3, 0x0A, 0x3D, 0x89, 0x38, 0xDE);
	union acpi_object *obj;
	struct acpi_device *adev;
	acpi_handle handle;
	u16 hid_descriptor_address;

	handle = ACPI_HANDLE(&client->dev);
	if (!handle || acpi_bus_get_device(handle, &adev)) {
		dev_err(&client->dev, "Error could not get ACPI device\n");
		return -ENODEV;
	}

	if (acpi_match_device_ids(adev, i2c_hid_acpi_blacklist) == 0)
		return -ENODEV;

	obj = acpi_evaluate_dsm_typed(handle, &i2c_hid_guid, 1, 1, NULL,
				      ACPI_TYPE_INTEGER);
	if (!obj) {
		dev_err(&client->dev, "Error _DSM call to get HID descriptor address failed\n");
		return -ENODEV;
	}

	hid_descriptor_address = obj->integer.value;
	ACPI_FREE(obj);

	return hid_descriptor_address;
}

static int i2c_hid_acpi_power_up_device(struct i2chid_subclass_data *subclass)
{
	struct i2c_hid_acpi *ihid_of =
		container_of(subclass, struct i2c_hid_acpi, subclass);
	struct device *dev = &ihid_of->client->dev;
	struct acpi_device *adev;

	/* Only need to call acpi_device_fix_up_power() the first time */
	if (ihid_of->power_fixed)
		return 0;
	ihid_of->power_fixed = true;

	adev = ACPI_COMPANION(dev);
	if (adev)
		acpi_device_fix_up_power(adev);

	return 0;
}

static int i2c_hid_acpi_probe(struct i2c_client *client,
			      const struct i2c_device_id *dev_id)
{
	struct device *dev = &client->dev;
	struct i2c_hid_acpi *ihid_acpi;
	u16 hid_descriptor_address;
	int ret;

	ihid_acpi = devm_kzalloc(&client->dev, sizeof(*ihid_acpi), GFP_KERNEL);
	if (!ihid_acpi)
		return -ENOMEM;

	ihid_acpi->client = client;
	ihid_acpi->subclass.power_up_device = i2c_hid_acpi_power_up_device;

	ret = i2c_hid_acpi_get_descriptor(client);
	if (ret < 0)
		return ret;
	hid_descriptor_address = ret;

	if (acpi_gbl_FADT.flags & ACPI_FADT_LOW_POWER_S0) {
		device_set_wakeup_capable(dev, true);
		device_set_wakeup_enable(dev, false);
	}

	return i2c_hid_core_probe(client, &ihid_acpi->subclass,
				  hid_descriptor_address);
}

static void i2c_hid_acpi_shutdown(struct i2c_client *client)
{
	i2c_hid_core_shutdown(client);
	acpi_device_set_power(ACPI_COMPANION(&client->dev), ACPI_STATE_D3_COLD);
}

static const struct dev_pm_ops i2c_hid_acpi_pm = {
	SET_SYSTEM_SLEEP_PM_OPS(i2c_hid_core_suspend, i2c_hid_core_resume)
};

static const struct acpi_device_id i2c_hid_acpi_match[] = {
	{"ACPI0C50", 0 },
	{"PNP0C50", 0 },
	{ },
};
MODULE_DEVICE_TABLE(acpi, i2c_hid_acpi_match);

static const struct i2c_device_id i2c_hid_acpi_id_table[] = {
	{ "hid", 0 },
	{ "hid-over-i2c", 0 },
	{ },
};
MODULE_DEVICE_TABLE(i2c, i2c_hid_acpi_id_table);

static struct i2c_driver i2c_hid_acpi_driver = {
	.driver = {
		.name	= "i2c_hid_acpi",
		.pm	= &i2c_hid_acpi_pm,
		.probe_type = PROBE_PREFER_ASYNCHRONOUS,
		.acpi_match_table = ACPI_PTR(i2c_hid_acpi_match),
	},

	.probe		= i2c_hid_acpi_probe,
	.remove		= i2c_hid_core_remove,
	.shutdown	= i2c_hid_acpi_shutdown,
	.id_table	= i2c_hid_acpi_id_table,
};

module_i2c_driver(i2c_hid_acpi_driver);

MODULE_DESCRIPTION("HID over I2C ACPI driver");
MODULE_AUTHOR("Benjamin Tissoires <benjamin.tissoires@gmail.com>");
MODULE_LICENSE("GPL");
