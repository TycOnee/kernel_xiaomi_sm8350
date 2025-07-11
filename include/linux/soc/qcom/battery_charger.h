/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2020, The Linux Foundation. All rights reserved.
 */

#ifndef _BATTERY_CHARGER_H
#define _BATTERY_CHARGER_H

enum battery_charger_prop {
	BATTERY_RESISTANCE,
	BATTERY_CHARGER_PROP_MAX,
};

#if defined(CONFIG_QTI_BATTERY_CHARGER) || defined(CONFIG_QTI_BATTERY_CHARGER_ODIN)
int qti_battery_charger_get_prop(const char *name,
				enum battery_charger_prop prop_id, int *val);
#else
static inline int
qti_battery_charger_get_prop(const char *name,
				enum battery_charger_prop prop_id, int *val)
{
	return -EINVAL;
}
#endif

#endif
