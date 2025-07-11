#ifndef __XIAOMI__TOUCH_H
#define __XIAOMI__TOUCH_H
#include <linux/device.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/wait.h>
#include <linux/types.h>
#include <linux/ioctl.h>
#include <linux/miscdevice.h>
#include <linux/proc_fs.h>
#include <linux/uaccess.h>
#include <linux/debugfs.h>
#include <linux/device.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/wait.h>
#include <linux/poll.h>
#include <linux/slab.h>
#include <linux/mempolicy.h>
#include <linux/dma-mapping.h>
#include <linux/export.h>
#include <linux/rtc.h>
#include <linux/seq_file.h>

/*CUR,DEFAULT,MIN,MAX*/
#define VALUE_TYPE_SIZE 6
#define VALUE_GRIP_SIZE 9
#define MAX_BUF_SIZE 256
#define BTN_INFO 0x152
#define MAX_TOUCH_ID 10

enum suspend_state {
	XIAOMI_TOUCH_RESUME = 0,
	XIAOMI_TOUCH_SUSPEND,
	XIAOMI_TOUCH_LP1,
	XIAOMI_TOUCH_LP2,
};

enum MODE_CMD {
	SET_CUR_VALUE = 0,
	GET_CUR_VALUE,
	GET_DEF_VALUE,
	GET_MIN_VALUE,
	GET_MAX_VALUE,
	GET_MODE_VALUE,
	RESET_MODE,
	SET_LONG_VALUE,
};

enum MODE_TYPE {
	Touch_Game_Mode        = 0,
	Touch_Active_MODE      = 1,
	Touch_UP_THRESHOLD     = 2,
	Touch_Tolerance        = 3,
	Touch_Wgh_Min          = 4,
	Touch_Wgh_Max          = 5,
	Touch_Wgh_Step         = 6,
	Touch_Edge_Filter      = 7,
	Touch_Panel_Orientation = 8,
	Touch_Report_Rate      = 9,
	Touch_Fod_Enable       = 10,
	Touch_Aod_Enable       = 11,
	Touch_Resist_RF        = 12,
	Touch_Idle_Time        = 13,
	Touch_Doubletap_Mode   = 14,
	Touch_Grip_Mode        = 15,
	Touch_FodIcon_Enable   = 16,
	Touch_Nonui_Mode       = 17,
	Touch_Debug_Level      = 18,
	Touch_Power_Status     = 19,
	Touch_Mode_NUM         = 20,
};

struct xiaomi_touch_interface {
	int touch_mode[Touch_Mode_NUM][VALUE_TYPE_SIZE];
	int (*setModeValue)(int Mode, int value);
	int (*setModeLongValue)(int Mode, int value_len, int *value);
	int (*getModeValue)(int Mode, int value_type);
	int (*getModeAll)(int Mode, int *modevalue);
	int (*resetMode)(int Mode);
	int (*prox_sensor_read)(void);
	int (*prox_sensor_write)(int on);
	int (*ear_sensor_write)(int on);
	int (*palm_sensor_read)(void);
	int (*palm_sensor_write)(int on);
	int (*get_touch_rx_num)(void);
	int (*get_touch_tx_num)(void);
	int (*get_touch_x_resolution)(void);
	int (*get_touch_y_resolution)(void);
	int (*enable_touch_raw)(bool en);
	int (*enable_clicktouch_raw)(int count);
	int (*enable_touch_delta)(bool en);
	u8 (*panel_vendor_read)(void);
	u8 (*panel_color_read)(void);
	u8 (*panel_display_read)(void);
	char (*touch_vendor_read)(void);
	int long_mode_len;
	int long_mode_value[MAX_BUF_SIZE];

	bool is_enable_touchraw;
	int thp_downthreshold;
	int thp_upthreshold;
	int thp_movethreshold;
	int thp_noisefilter;
	int thp_islandthreshold;
	int thp_smooth;
	bool is_enable_touchdelta;
	bool ear_sensor_switch;
	bool ear_sensor_value;
};

struct xiaomi_touch {
	struct miscdevice 	misc_dev;
	struct device *dev;
	struct class *class;
	struct attribute_group attrs;
	struct mutex  mutex;
	struct mutex  palm_mutex;
	struct mutex  prox_mutex;
	wait_queue_head_t 	wait_queue;
};

#define LAST_TOUCH_EVENTS_MAX 512

enum touch_state {
	EVENT_INIT,
	EVENT_DOWN,
	EVENT_UP,
};

struct touch_event {
	u32 slot;
	enum touch_state state;
	struct timespec touch_time;
};

struct last_touch_event {
	int head;
	struct touch_event touch_event_buf[LAST_TOUCH_EVENTS_MAX];
};

struct xiaomi_touch_pdata{
	struct xiaomi_touch *device;
	struct xiaomi_touch_interface *touch_data[2];
	int suspend_state;
	dma_addr_t phy_base;
	unsigned int *raw_data;
	int palm_value;
	bool palm_changed;
	int prox_value;
	bool prox_changed;
	bool set_update;
	bool bump_sample_rate;
	const char *name;
	struct proc_dir_entry  *last_touch_events_proc;
	struct last_touch_event *last_touch_events;
};

struct xiaomi_touch *xiaomi_touch_dev_get(int minor);

extern struct class *get_xiaomi_touch_class(void);

extern struct device *get_xiaomi_touch_dev(void);

extern int update_palm_sensor_value(int value);

extern int update_prox_sensor_value(int value);

extern int xiaomitouch_register_modedata(int touchId, struct xiaomi_touch_interface *data);

extern int copy_touch_rawdata(char *raw_base,  int len);

extern int update_touch_rawdata(void);

extern int update_clicktouch_raw(void);

extern int update_ear_sensor(void);

extern void last_touch_events_collect(int slot, int state);

int xiaomi_touch_set_suspend_state(int state);

#endif
