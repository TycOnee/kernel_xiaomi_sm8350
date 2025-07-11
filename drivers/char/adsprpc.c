// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2012-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2023, Qualcomm Innovation Center, Inc. All rights reserved.
 */

/* Uncomment this block to log an error on every VERIFY failure */
/*
 * #ifndef VERIFY_PRINT_ERROR
 * #define VERIFY_PRINT_ERROR
 * #endif
 */

#include <linux/dma-buf.h>
#include <linux/dma-mapping.h>
#include <linux/slab.h>
#include <linux/completion.h>
#include <linux/pagemap.h>
#include <linux/mm.h>
#include <linux/wait.h>
#include <linux/sched.h>
#include <linux/module.h>
#include <linux/cdev.h>
#include <linux/list.h>
#include <linux/hash.h>
#include <linux/msm_ion.h>
#include <soc/qcom/secure_buffer.h>
#include <linux/rpmsg.h>
#include <linux/ipc_logging.h>
#include <soc/qcom/subsystem_notif.h>
#include <soc/qcom/subsystem_restart.h>
#include <soc/qcom/service-notifier.h>
#include <soc/qcom/service-locator.h>
#include <linux/scatterlist.h>
#include <linux/uaccess.h>
#include <linux/device.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/dma-contiguous.h>
#include <linux/cma.h>
#include <linux/iommu.h>
#include <linux/sort.h>
#include <linux/cred.h>
#include <linux/msm_dma_iommu_mapping.h>
#include "adsprpc_compat.h"
#include "adsprpc_shared.h"
#include <soc/qcom/ramdump.h>
#include <soc/qcom/minidump.h>
#include <linux/delay.h>
#include <linux/debugfs.h>
#include <linux/pm_qos.h>
#include <linux/stat.h>
#include <linux/preempt.h>
#include <linux/of_reserved_mem.h>

#define CREATE_TRACE_POINTS
#include <trace/events/fastrpc.h>

#define TZ_PIL_PROTECT_MEM_SUBSYS_ID 0x0C
#define TZ_PIL_CLEAR_PROTECT_MEM_SUBSYS_ID 0x0D
#define TZ_PIL_AUTH_QDSP6_PROC 1

#define FASTRPC_DMAHANDLE_NOMAP (16)

#define FASTRPC_ENOSUCH 39
#define DEBUGFS_SIZE 3072
#define PID_SIZE 10

#define AUDIO_PDR_ADSP_DTSI_PROPERTY_NAME        "qcom,fastrpc-adsp-audio-pdr"
#define AUDIO_PDR_SERVICE_LOCATION_CLIENT_NAME   "audio_pdr_adsprpc"
#define AUDIO_PDR_ADSP_SERVICE_NAME              "avs/audio"
#define ADSP_AUDIOPD_NAME                        "msm/adsp/audio_pd"

#define SENSORS_PDR_ADSP_DTSI_PROPERTY_NAME        "qcom,fastrpc-adsp-sensors-pdr"
#define SENSORS_PDR_ADSP_SERVICE_LOCATION_CLIENT_NAME   "sensors_pdr_adsprpc"
#define SENSORS_PDR_ADSP_SERVICE_NAME              "tms/servreg"
#define ADSP_SENSORPD_NAME                       "msm/adsp/sensor_pd"

#define SENSORS_PDR_SLPI_DTSI_PROPERTY_NAME      "qcom,fastrpc-slpi-sensors-pdr"
#define SENSORS_PDR_SLPI_SERVICE_LOCATION_CLIENT_NAME "sensors_pdr_sdsprpc"
#define SENSORS_PDR_SLPI_SERVICE_NAME            SENSORS_PDR_ADSP_SERVICE_NAME
#define SLPI_SENSORPD_NAME                       "msm/slpi/sensor_pd"

#define FASTRPC_SECURE_WAKE_SOURCE_CLIENT_NAME		"adsprpc-secure"
#define FASTRPC_NON_SECURE_WAKE_SOURCE_CLIENT_NAME	"adsprpc-non_secure"

#define RPC_TIMEOUT	(5 * HZ)
#define BALIGN		128
#define NUM_CHANNELS	4	/* adsp, mdsp, slpi, cdsp*/
#define NUM_SESSIONS	13	/* max 12 compute, 1 cpz */
#define M_FDLIST	(16)
#define M_CRCLIST	(64)
#define M_KERNEL_PERF_LIST (PERF_KEY_MAX)
#define M_DSP_PERF_LIST (12)

#define SESSION_ID_INDEX (30)
#define FASTRPC_CTX_MAGIC (0xbeeddeed)

/*
 * Fastrpc context ID bit-map:
 *
 * bits 0-3   : type of remote PD
 * bit  4     : type of job (sync/async)
 * bit  5     : reserved
 * bits 6-15  : index in context table
 * bits 16-63 : incrementing context ID
 */
#define FASTRPC_CTX_MAX (1024)

#define FASTRPC_CTX_JOB_TYPE_POS (4)
#define FASTRPC_CTX_TABLE_IDX_POS (6)
#define FASTRPC_CTX_JOBID_POS (16)
#define FASTRPC_CTX_TABLE_IDX_MASK \
	((FASTRPC_CTX_MAX - 1) << FASTRPC_CTX_TABLE_IDX_POS)
#define FASTRPC_ASYNC_JOB_MASK   (1)

#define GET_TABLE_IDX_FROM_CTXID(ctxid) \
	((ctxid & FASTRPC_CTX_TABLE_IDX_MASK) >> FASTRPC_CTX_TABLE_IDX_POS)

#define VALID_FASTRPC_CID(cid) \
	(cid >= ADSP_DOMAIN_ID && cid < NUM_CHANNELS)

/* Reserve few entries in context table for critical kernel and static RPC
 * calls to avoid user invocations from exhausting all entries.
 */
#define NUM_KERNEL_AND_STATIC_ONLY_CONTEXTS (70)

/* Maximum number of pending contexts per remote session */
#define MAX_PENDING_CTX_PER_SESSION (64)

#define NUM_DEVICES   2 /* adsprpc-smd, adsprpc-smd-secure */
#define MINOR_NUM_DEV 0
#define MINOR_NUM_SECURE_DEV 1
#define NON_SECURE_CHANNEL 0
#define SECURE_CHANNEL 1

#define IS_CACHE_ALIGNED(x) (((x) & ((L1_CACHE_BYTES)-1)) == 0)
#ifndef ION_FLAG_CACHED
#define ION_FLAG_CACHED (1)
#endif

#define ADSP_DOMAIN_ID (0)
#define MDSP_DOMAIN_ID (1)
#define SDSP_DOMAIN_ID (2)
#define CDSP_DOMAIN_ID (3)

#define UNSIGNED_PD_SUPPORT 1
/*
 * ctxid of every message is OR-ed with fl->pd (0/1/2) before
 * it is sent to DSP. So mask 2 LSBs to retrieve actual context
 */
#define CONTEXT_PD_CHECK (3)

#define RH_CID ADSP_DOMAIN_ID

#define FASTRPC_STATIC_HANDLE_PROCESS_GROUP (1)
#define FASTRPC_STATIC_HANDLE_DSP_UTILITIES (2)
#define FASTRPC_STATIC_HANDLE_LISTENER (3)
#define FASTRPC_STATIC_HANDLE_MAX (20)
#define FASTRPC_LATENCY_CTRL_ENB  (1)

/* Maximum PM timeout that can be voted through fastrpc */
#define MAX_PM_TIMEOUT_MS 50

/* timeout in us for busy polling after early response from remote processor */
#define FASTRPC_POLL_TIME (4000)

/* timeout in us for polling without preempt */
#define FASTRPC_POLL_TIME_WITHOUT_PREEMPT (500)

/* timeout in us for polling completion signal after user early hint */
#define FASTRPC_USER_EARLY_HINT_TIMEOUT (500)

/* Early wake up poll completion number received from remote processor */
#define FASTRPC_EARLY_WAKEUP_POLL (0xabbccdde)

/* latency in us, early wake up signal used below this value */
#define FASTRPC_EARLY_WAKEUP_LATENCY (200)

/* response version number */
#define FASTRPC_RSP_VERSION2 (2)

/* CPU feature information to DSP */
#define FASTRPC_CPUINFO_DEFAULT (0)
#define FASTRPC_CPUINFO_EARLY_WAKEUP (1)

#define INIT_FILELEN_MAX (2*1024*1024)
#define INIT_MEMLEN_MAX  (8*1024*1024)
#define MAX_CACHE_BUF_SIZE (8*1024*1024)

/* Maximum buffers cached in cached buffer list */
#define MAX_CACHED_BUFS   (32)

/* Max no. of persistent headers pre-allocated per process */
#define MAX_PERSISTENT_HEADERS    (25)

/* Length of glink transaction history to store */
#define GLINK_MSG_HISTORY_LEN (128)

#define PERF_CAPABILITY   (1 << 1)

/*Max number of region supported  */
#define MAX_UNIQUE_ID 5

/*Unique index flag used for mini dump*/

static int md_unique_index_flag[MAX_UNIQUE_ID] = { 0, 0, 0, 0, 0 };

enum fastrpc_proc_attr {
	/* Macro for Debug attr */
	FASTRPC_MODE_DEBUG				= 1 << 0,
	/* Macro for Ptrace */
	FASTRPC_MODE_PTRACE				= 1 << 1,
	/* Macro for CRC Check */
	FASTRPC_MODE_CRC				= 1 << 2,
	/* Macro for Unsigned PD */
	FASTRPC_MODE_UNSIGNED_MODULE	= 1 << 3,
	/* Macro for Adaptive QoS */
	FASTRPC_MODE_ADAPTIVE_QOS		= 1 << 4,
	/* Macro for System Process */
	FASTRPC_MODE_SYSTEM_PROCESS		= 1 << 5,
	/* Macro for Prvileged Process */
	FASTRPC_MODE_PRIVILEGED      = (1 << 6),
};

/* Type of fastrpc DMA bufs sent to DSP */
enum fastrpc_buf_type {
	METADATA_BUF,
	COPYDATA_BUF,
	INITMEM_BUF,
	USERHEAP_BUF,
};

/* Types of RPC calls to DSP */
enum fastrpc_msg_type {
	USER_MSG = 0,
	KERNEL_MSG_WITH_ZERO_PID,
	KERNEL_MSG_WITH_NONZERO_PID,
};

#define PERF_END (void)0

#define PERF(enb, cnt, ff) \
	{\
		struct timespec64 startT = {0};\
		uint64_t *counter = cnt;\
		if (enb && counter) {\
			ktime_get_real_ts64(&startT);\
		} \
		ff ;\
		if (enb && counter) {\
			*counter += getnstimediff(&startT);\
		} \
	}

#define GET_COUNTER(perf_ptr, offset)  \
	(perf_ptr != NULL ?\
		(((offset >= 0) && (offset < PERF_KEY_MAX)) ?\
			(uint64_t *)(perf_ptr + offset)\
				: (uint64_t *)NULL) : (uint64_t *)NULL)

/* Macro for comparing local client and PD names with those from callback */
#define COMPARE_SERVICE_LOCATOR_NAMES(cb_client, local_client, \
	cb_pdname, local_pdname) \
		((!strcmp(cb_client, local_client)) \
		&& (!strcmp(cb_pdname, local_pdname)))

#define IS_ASYNC_FASTRPC_AVAILABLE (1)

/* User PD Dump Name Max length */
#define RAMDUMP_NAME_MAX_LENGTH 20

static int fastrpc_pdr_notifier_cb(struct notifier_block *nb,
					unsigned long code,
					void *data);
static struct dentry *debugfs_root;
static struct dentry *debugfs_global_file;

static inline uint64_t buf_page_start(uint64_t buf)
{
	uint64_t start = (uint64_t) buf & PAGE_MASK;
	return start;
}

static inline uint64_t buf_page_offset(uint64_t buf)
{
	uint64_t offset = (uint64_t) buf & (PAGE_SIZE - 1);
	return offset;
}

static inline uint64_t buf_num_pages(uint64_t buf, size_t len)
{
	uint64_t start = buf_page_start(buf) >> PAGE_SHIFT;
	uint64_t end = (((uint64_t) buf + len - 1) & PAGE_MASK) >> PAGE_SHIFT;
	uint64_t nPages = end - start + 1;
	return nPages;
}

static inline uint64_t buf_page_size(uint32_t size)
{
	uint64_t sz = (size + (PAGE_SIZE - 1)) & PAGE_MASK;

	return sz > PAGE_SIZE ? sz : PAGE_SIZE;
}

static inline void *uint64_to_ptr(uint64_t addr)
{
	void *ptr = (void *)((uintptr_t)addr);

	return ptr;
}

static inline uint64_t ptr_to_uint64(void *ptr)
{
	uint64_t addr = (uint64_t)((uintptr_t)ptr);

	return addr;
}

struct secure_vm {
	int *vmid;
	int *vmperm;
	int vmcount;
};

struct gid_list {
	unsigned int *gids;
	unsigned int gidcount;
};

struct qos_cores {
	int *coreno;
	int corecount;
};

struct fastrpc_file;

struct fastrpc_buf {
	struct hlist_node hn;
	struct hlist_node hn_rem;
	struct hlist_node hn_init;
	struct fastrpc_file *fl;
	void *virt;
	uint64_t phys;
	size_t size;
	unsigned long dma_attr;
	uintptr_t raddr;
	uint32_t flags;
	int type;		/* One of "fastrpc_buf_type" */
	bool in_use;	/* Used only for persistent header buffers */
	struct timespec64 buf_start_time;
	struct timespec64 buf_end_time;
};

struct fastrpc_ctx_lst;

struct fastrpc_tx_msg {
	struct smq_msg msg; /* Msg sent to remote subsystem */
	int rpmsg_send_err; /* rpmsg error */
	int64_t ns;         /* Timestamp (in ns) of msg */
};

struct fastrpc_rx_msg {
	struct smq_invoke_rspv2 rsp;  /* Response from remote subsystem */
	int64_t ns;   /* Timestamp (in ns) of response */
};

struct fastrpc_rpmsg_log {
	unsigned int tx_index;  /* Current index of 'tx_msgs' array */
	unsigned int rx_index;  /* Current index of 'rx_msgs' array */

	/* Rolling history of messages sent to remote subsystem */
	struct fastrpc_tx_msg tx_msgs[GLINK_MSG_HISTORY_LEN];

	/* Rolling history of responses from remote subsystem */
	struct fastrpc_rx_msg rx_msgs[GLINK_MSG_HISTORY_LEN];
	spinlock_t lock;
};

struct overlap {
	uintptr_t start;
	uintptr_t end;
	int raix;
	uintptr_t mstart;
	uintptr_t mend;
	uintptr_t offset;
	int do_cmo;		/*used for cache maintenance of inrout buffers*/
};

struct fastrpc_perf {
	uint64_t count;
	uint64_t flush;
	uint64_t map;
	uint64_t copy;
	uint64_t link;
	uint64_t getargs;
	uint64_t putargs;
	uint64_t invargs;
	uint64_t invoke;
	uint64_t tid;
};

struct smq_invoke_ctx {
	struct hlist_node hn;
	/* Async node to add to async job ctx list */
	struct list_head asyncn;
	struct completion work;
	int retval;
	int pid;
	int tgid;
	remote_arg_t *lpra;
	remote_arg64_t *rpra;
	remote_arg64_t *lrpra;		/* Local copy of rpra for put_args */
	int *fds;
	unsigned int *attrs;
	struct fastrpc_mmap **maps;
	struct fastrpc_buf *buf;
	struct fastrpc_buf *copybuf;	/*used to copy non-ion buffers */
	size_t used;
	struct fastrpc_file *fl;
	uint32_t handle;
	uint32_t sc;
	struct overlap *overs;
	struct overlap **overps;
	struct smq_msg msg;
	uint32_t *crc;
	uint64_t *perf_kernel;
	uint64_t *perf_dsp;
	unsigned int magic;
	uint64_t ctxid;
	struct fastrpc_perf *perf;
	/* response flags from remote processor */
	enum fastrpc_response_flags rsp_flags;
	/* user hint of completion time in us */
	uint32_t early_wake_time;
	/* work done status flag */
	bool is_work_done;
	/* Store Async job in the context*/
	struct fastrpc_async_job asyncjob;
	/* Async early flag to check the state of context */
	bool is_early_wakeup;
	uint32_t sc_interrupted;
	struct fastrpc_file *fl_interrupted;
	uint32_t handle_interrupted;
};

struct fastrpc_ctx_lst {
	struct hlist_head pending;
	struct hlist_head interrupted;
	/* Number of active contexts queued to DSP */
	uint32_t num_active_ctxs;
	/* Queue which holds all async job contexts of process */
	struct list_head async_queue;
};

struct fastrpc_smmu {
	struct device *dev;
	const char *dev_name;
	int cb;
	int enabled;
	int faults;
	int secure;
	int coherent;
};

struct fastrpc_session_ctx {
	struct device *dev;
	struct fastrpc_smmu smmu;
	int used;
};

struct fastrpc_static_pd {
	char *servloc_name;
	char *spdname;
	struct notifier_block pdrnb;
	struct notifier_block get_service_nb;
	void *pdrhandle;
	uint64_t pdrcount;
	uint64_t prevpdrcount;
	int ispdup;
	int cid;
};

struct fastrpc_dsp_capabilities {
	uint32_t is_cached;	//! Flag if dsp attributes are cached
	uint32_t dsp_attributes[FASTRPC_MAX_DSP_ATTRIBUTES];
};

struct fastrpc_channel_ctx {
	char *name;
	char *subsys;
	struct rpmsg_device *rpdev;
	struct device *dev;
	struct fastrpc_session_ctx session[NUM_SESSIONS];
	struct fastrpc_static_pd spd[NUM_SESSIONS];
	struct completion work;
	struct completion workport;
	struct notifier_block nb;
	struct mutex smd_mutex;
	struct mutex rpmsg_mutex;
	uint64_t sesscount;
	uint64_t ssrcount;
	void *handle;
	uint64_t prevssrcount;
	int issubsystemup;
	int vmid;
	struct secure_vm rhvm;
	int ramdumpenabled;
	void *rh_dump_dev;
	/* Indicates, if channel is restricted to secure node only */
	int secure;
	/* Indicates whether the channel supports unsigned PD */
	bool unsigned_support;
	struct fastrpc_dsp_capabilities dsp_cap_kernel;
	/* cpu capabilities shared to DSP */
	uint64_t cpuinfo_todsp;
	bool cpuinfo_status;
	struct smq_invoke_ctx *ctxtable[FASTRPC_CTX_MAX];
	spinlock_t ctxlock;
	struct fastrpc_rpmsg_log gmsg_log;
	struct hlist_head initmems;
};

struct fastrpc_apps {
	struct fastrpc_channel_ctx *channel;
	struct cdev cdev;
	struct class *class;
	struct smq_phy_page range;
	struct hlist_head maps;
	uint32_t staticpd_flags;
	dev_t dev_no;
	int compat;
	struct hlist_head drivers;
	spinlock_t hlock;
	struct device *dev;
	/* Indicates fastrpc device node info */
	struct device *dev_fastrpc;
	unsigned int latency;
	int rpmsg_register;
	bool legacy_remote_heap;
	/* Unique job id for each message */
	uint64_t jobid[NUM_CHANNELS];
	struct gid_list gidlist;
	struct device *secure_dev;
	struct device *non_secure_dev;
	/* Secure subsystems like ADSP/SLPI will use secure client */
	struct wakeup_source *wake_source_secure;
	/* Non-secure subsystem like CDSP will use regular client */
	struct wakeup_source *wake_source;
	uint32_t duplicate_rsp_err_cnt;
	struct qos_cores silvercores;
	uint32_t max_size_limit;
	void *ramdump_handle;
	bool enable_ramdump;
	struct mutex mut_uid;
	/* Indicates cdsp device status */
	int remote_cdsp_status;
};

struct fastrpc_mmap {
	struct hlist_node hn;
	struct fastrpc_file *fl;
	struct fastrpc_apps *apps;
	int fd;
	uint32_t flags;
	struct dma_buf *buf;
	struct sg_table *table;
	struct dma_buf_attachment *attach;
	struct ion_handle *handle;
	uint64_t phys;
	size_t size;
	uintptr_t va;
	size_t len;
	int refs;
	uintptr_t raddr;
	int uncached;
	int secure;
	bool is_persistent;		/* Indicates whether map is persistent */
	int frpc_md_index;		/* Minidump unique index */
	uintptr_t attr;
	bool in_use;			/* Indicates if persistent map is in use */
	struct timespec64 map_start_time;
	struct timespec64 map_end_time;
	bool is_filemap; /* flag to indicate map used in process init */
	unsigned int ctx_refs; /* Indicates reference count for context map */
};

enum fastrpc_perfkeys {
	PERF_COUNT = 0,
	PERF_FLUSH = 1,
	PERF_MAP = 2,
	PERF_COPY = 3,
	PERF_LINK = 4,
	PERF_GETARGS = 5,
	PERF_PUTARGS = 6,
	PERF_INVARGS = 7,
	PERF_INVOKE = 8,
	PERF_TID = 9,
	PERF_KEY_MAX = 10,
};

struct fastrpc_file {
	struct hlist_node hn;
	spinlock_t hlock;
	struct hlist_head maps;
	struct hlist_head cached_bufs;
	uint32_t num_cached_buf;
	struct hlist_head remote_bufs;
	struct fastrpc_ctx_lst clst;
	struct fastrpc_session_ctx *sctx;
	struct fastrpc_buf *init_mem;

	/* No. of persistent headers */
	unsigned int num_pers_hdrs;
	/* Pre-allocated header buffer */
	struct fastrpc_buf *pers_hdr_buf;
	/* Pre-allocated buffer divided into N chunks */
	struct fastrpc_buf *hdr_bufs;

	struct fastrpc_session_ctx *secsctx;
	uint32_t mode;
	uint32_t profile;
	int sessionid;
	int tgid_open;	/* Process ID during device open */
	int tgid;		/* Process ID that uses device for RPC calls */
	int cid;
	uint64_t ssrcount;
	int pd;
	char *servloc_name;
	int file_close;
	int dsp_proc_init;
	struct fastrpc_apps *apps;
	struct dentry *debugfs_file;
	struct dev_pm_qos_request *dev_pm_qos_req;
	int qos_request;
	struct mutex map_mutex;
	struct mutex internal_map_mutex;
	/* Identifies the device (MINOR_NUM_DEV / MINOR_NUM_SECURE_DEV) */
	int dev_minor;
	char *debug_buf;
	/* Flag to indicate attempt has been made to allocate memory for debug_buf*/
	int debug_buf_alloced_attempted;
	/* Flag to enable PM wake/relax voting for every remote invoke */
	int wake_enable;
	struct gid_list gidlist;
	/* Number of jobs pending in Async Queue */
	atomic_t async_queue_job_count;
	/* Async wait queue to synchronize glink response and async thread */
	wait_queue_head_t async_wait_queue;
	/* IRQ safe spin lock for protecting async queue */
	spinlock_t aqlock;
	uint32_t ws_timeout;
	bool untrusted_process;
	/* Process kill will wait on work when ram dump collection in progress */
	struct completion work;
	/* Flag to indicate ram dump collection status*/
	bool is_ramdump_pend;
	/* Flag to indicate dynamic process creation status*/
	enum fastrpc_process_create_state dsp_process_state;
};

static struct fastrpc_apps gfa;

static struct fastrpc_channel_ctx gcinfo[NUM_CHANNELS] = {
	{
		.name = "adsprpc-smd",
		.subsys = "adsp",
		.spd = {
			{
				.servloc_name =
					AUDIO_PDR_SERVICE_LOCATION_CLIENT_NAME,
				.spdname = ADSP_AUDIOPD_NAME,
				.pdrnb.notifier_call =
						fastrpc_pdr_notifier_cb,
				.cid = ADSP_DOMAIN_ID,
			},
			{
				.servloc_name =
				SENSORS_PDR_ADSP_SERVICE_LOCATION_CLIENT_NAME,
				.spdname = ADSP_SENSORPD_NAME,
				.pdrnb.notifier_call =
						fastrpc_pdr_notifier_cb,
				.cid = ADSP_DOMAIN_ID,
			}
		},
		.cpuinfo_todsp = FASTRPC_CPUINFO_DEFAULT,
		.cpuinfo_status = false,
	},
	{
		.name = "mdsprpc-smd",
		.subsys = "modem",
		.spd = {
			{
				.cid = MDSP_DOMAIN_ID,
			}
		},
		.cpuinfo_todsp = FASTRPC_CPUINFO_DEFAULT,
		.cpuinfo_status = false,
	},
	{
		.name = "sdsprpc-smd",
		.subsys = "slpi",
		.spd = {
			{
				.servloc_name =
				SENSORS_PDR_SLPI_SERVICE_LOCATION_CLIENT_NAME,
				.spdname = SLPI_SENSORPD_NAME,
				.pdrnb.notifier_call =
						fastrpc_pdr_notifier_cb,
				.cid = SDSP_DOMAIN_ID,
			}
		},
		.cpuinfo_todsp = FASTRPC_CPUINFO_DEFAULT,
		.cpuinfo_status = false,
	},
	{
		.name = "cdsprpc-smd",
		.subsys = "cdsp",
		.spd = {
			{
				.cid = CDSP_DOMAIN_ID,
			}
		},
		.cpuinfo_todsp = FASTRPC_CPUINFO_EARLY_WAKEUP,
		.cpuinfo_status = false,
	},
};

static int hlosvm[1] = {VMID_HLOS};
static int hlosvmperm[1] = {PERM_READ | PERM_WRITE | PERM_EXEC};

static uint32_t kernel_capabilities[FASTRPC_MAX_ATTRIBUTES -
					FASTRPC_MAX_DSP_ATTRIBUTES] = {
	PERF_CAPABILITY,
	/* PERF_LOGGING_V2_SUPPORT feature is supported, unsupported = 0 */
	1
	/* Fastrpc Driver error code changes present */
};

static inline void fastrpc_pm_awake(struct fastrpc_file *fl, int channel_type);
static int fastrpc_mem_map_to_dsp(struct fastrpc_file *fl, int fd, int offset,
				uint32_t flags, uintptr_t va, uint64_t phys,
				size_t size, uintptr_t *raddr);

static inline int64_t getnstimediff(struct timespec64 *start)
{
	int64_t ns;
	struct timespec64 ts, b;

	ktime_get_real_ts64(&ts);
	b = timespec64_sub(ts, *start);
	ns = timespec64_to_ns(&b);
	return ns;
}

/**
 * get_timestamp_in_ns - Gets time of day in nanoseconds
 *
 * Returns: Timestamp in nanoseconds
 */
static inline int64_t get_timestamp_in_ns(void)
{
	int64_t ns = 0;
	struct timespec64 ts;

	ktime_get_real_ts64(&ts);
	ns = timespec64_to_ns(&ts);
	return ns;
}
static inline int poll_on_early_response(struct smq_invoke_ctx *ctx)
{
	int ii, jj, err = -EIO;
	uint32_t sc = ctx->sc;
	struct smq_invoke_buf *list;
	struct smq_phy_page *pages;
	uint64_t *fdlist = NULL;
	uint32_t *crclist = NULL, *poll = NULL;
	unsigned int inbufs, outbufs, handles;

	/* calculate poll memory location */
	inbufs = REMOTE_SCALARS_INBUFS(sc);
	outbufs = REMOTE_SCALARS_OUTBUFS(sc);
	handles = REMOTE_SCALARS_INHANDLES(sc) + REMOTE_SCALARS_OUTHANDLES(sc);
	list = smq_invoke_buf_start(ctx->rpra, sc);
	pages = smq_phy_page_start(sc, list);
	fdlist = (uint64_t *)(pages + inbufs + outbufs + handles);
	crclist = (uint32_t *)(fdlist + M_FDLIST);
	poll = (uint32_t *)(crclist + M_CRCLIST);

	/*
	 * poll on memory for actual completion after receiving
	 * early response from DSP. Return failure on timeout.
	 */
	preempt_disable();
	for (ii = 0, jj = 0; ii < FASTRPC_POLL_TIME; ii++, jj++) {
		if (*poll == FASTRPC_EARLY_WAKEUP_POLL) {
			err = 0;
			break;
		}
		if (jj == FASTRPC_POLL_TIME_WITHOUT_PREEMPT) {
			/* limit preempt disable time with no rescheduling */
			preempt_enable();
			/* Wait for DSP to finish updating poll memory */
			rmb();
			preempt_disable();
			jj = 0;
		}
		udelay(1);
	}
	preempt_enable();
	return err;
}

/**
 * fastrpc_update_txmsg_buf - Update history of sent glink messages
 * @chan           : Channel context
 * @msg            : Pointer to RPC message to remote subsystem
 * @rpmsg_send_err : Error from rpmsg
 * @ns             : Timestamp (in ns) of sent message
 *
 * Returns none
 */
static inline void fastrpc_update_txmsg_buf(struct fastrpc_channel_ctx *chan,
	struct smq_msg *msg, int rpmsg_send_err, int64_t ns)
{
	unsigned long flags = 0;
	unsigned int tx_index = 0;
	struct fastrpc_tx_msg *tx_msg = NULL;

	spin_lock_irqsave(&chan->gmsg_log.lock, flags);

	tx_index = chan->gmsg_log.tx_index;
	tx_msg = &chan->gmsg_log.tx_msgs[tx_index];

	memcpy(&tx_msg->msg, msg, sizeof(struct smq_msg));
	tx_msg->rpmsg_send_err = rpmsg_send_err;
	tx_msg->ns = ns;

	tx_index++;
	chan->gmsg_log.tx_index =
		(tx_index > (GLINK_MSG_HISTORY_LEN - 1)) ? 0 : tx_index;

	spin_unlock_irqrestore(&chan->gmsg_log.lock, flags);
}

/**
 * fastrpc_update_rxmsg_buf - Update history of received glink responses
 * @chan            : Channel context
 * @ctx             : Context of received response from DSP
 * @retval          : Return value for RPC call
 * @rsp_flags       : Response type
 * @early_wake_time : Poll time for early wakeup
 * @ver             : Version of response
 * @ns              : Timestamp (in ns) of response
 *
 * Returns none
 */
static inline void fastrpc_update_rxmsg_buf(struct fastrpc_channel_ctx *chan,
	uint64_t ctx, int retval, uint32_t rsp_flags,
	uint32_t early_wake_time, uint32_t ver, int64_t ns)
{
	unsigned long flags = 0;
	unsigned int rx_index = 0;
	struct fastrpc_rx_msg *rx_msg = NULL;
	struct smq_invoke_rspv2 *rsp = NULL;

	spin_lock_irqsave(&chan->gmsg_log.lock, flags);

	rx_index = chan->gmsg_log.rx_index;
	rx_msg = &chan->gmsg_log.rx_msgs[rx_index];
	rsp = &rx_msg->rsp;

	rsp->ctx = ctx;
	rsp->retval = retval;
	rsp->flags = rsp_flags;
	rsp->early_wake_time = early_wake_time;
	rsp->version = ver;
	rx_msg->ns = ns;

	rx_index++;
	chan->gmsg_log.rx_index =
		(rx_index > (GLINK_MSG_HISTORY_LEN - 1)) ? 0 : rx_index;

	spin_unlock_irqrestore(&chan->gmsg_log.lock, flags);
}


static inline int get_unique_index(void)
{
	int index = -1;

	mutex_lock(&gfa.mut_uid);
	for (index = 0; index < MAX_UNIQUE_ID; index++) {
		if (md_unique_index_flag[index] == 0) {
			md_unique_index_flag[index] = 1;
			mutex_unlock(&gfa.mut_uid);
			return index;
		}
	}
	mutex_unlock(&gfa.mut_uid);
	return index;
}

static inline void reset_unique_index(int index)
{
	mutex_lock(&gfa.mut_uid);
	if (index > -1 && index < MAX_UNIQUE_ID)
		md_unique_index_flag[index] = 0;
	mutex_unlock(&gfa.mut_uid);
}

/**
 * fastrpc_elf_ramdump - Dump given ram dump entry
 * @rh_dump_dev       : Device handle for given channel
 * @ramdump_entry     : Dump region entry
 *
 * Returns int
 */
static int fastrpc_elf_ramdump(void *rh_dump_dev, struct ramdump_segment *ramdump_seg)
{
	int err = 0;

	err = do_minidump(rh_dump_dev,
			ramdump_seg, 1);

	return err;
}

/**
 * fastrpc_minidump_add_region - Add mini dump region
 * @fastrpc_mmap       : Input structure mmap
 *
 * Returns int
 */
static int fastrpc_minidump_add_region(struct fastrpc_mmap *map)
{
	int err = 0, ret_val = 0, md_index = 0;
	struct md_region md_entry;

	md_index = get_unique_index();
	if (md_index > -1 && md_index < MAX_UNIQUE_ID) {
		scnprintf(md_entry.name, MAX_NAME_LENGTH, "FRPC_%d", md_index);
		md_entry.virt_addr = map->va;
		md_entry.phys_addr = map->phys;
		md_entry.size = map->size;
		ret_val = msm_minidump_add_region(&md_entry);
		if (ret_val < 0) {
			ADSPRPC_ERR(
			"Failed to add/update CMA to Minidump for phys: 0x%llx, size: %zu, md_index %d, md_entry.name %s\n",
			map->phys,
			map->size, md_index,
			md_entry.name);
			reset_unique_index(md_index);
			err = ret_val;
		} else {
			map->frpc_md_index = md_index;
		}
	} else {
		pr_warn("failed to generate valid unique id for mini dump : %d\n", md_index);
	}
	return err;
}

/**
 * fastrpc_minidump_remove_region - Remove mini dump region if added
 * @fastrpc_mmap       : Input structure mmap
 *
 * Returns int
 */
static int fastrpc_minidump_remove_region(struct fastrpc_mmap *map)
{
	int err = -1;
	struct md_region md_entry;

	if (map->frpc_md_index > -1 && map->frpc_md_index < MAX_UNIQUE_ID) {
		scnprintf(md_entry.name, MAX_NAME_LENGTH, "FRPC_%d",
					map->frpc_md_index);
		md_entry.virt_addr = map->va;
		md_entry.phys_addr = map->phys;
		md_entry.size = map->size;
		err = msm_minidump_remove_region(&md_entry);
		if (err < 0) {
			ADSPRPC_ERR(
				"Failed to remove CMA from Minidump for phys: 0x%llx, size: %zu index = %d\n",
				 map->phys, map->size, map->frpc_md_index);
		} else {
			reset_unique_index(map->frpc_md_index);
			map->frpc_md_index = -1;
		}
	} else {
		ADSPRPC_WARN("mini-dump enabled with invalid unique id: %d\n", map->frpc_md_index);
	}
	return err;
}


static void fastrpc_buf_free(struct fastrpc_buf *buf, int cache)
{
	struct fastrpc_file *fl = buf == NULL ? NULL : buf->fl;
	int vmid, err = 0, cid = -1;

	if (!fl)
		return;
	if (buf->in_use) {
		/* Don't free persistent header buf. Just mark as available */
		spin_lock(&fl->hlock);
		buf->in_use = false;
		spin_unlock(&fl->hlock);
		return;
	}
	if (cache && buf->size < MAX_CACHE_BUF_SIZE) {
		spin_lock(&fl->hlock);
		if (fl->num_cached_buf > MAX_CACHED_BUFS) {
			spin_unlock(&fl->hlock);
			goto skip_buf_cache;
		}
		hlist_add_head(&buf->hn, &fl->cached_bufs);
		fl->num_cached_buf++;
		buf->type = -1;
		spin_unlock(&fl->hlock);
		return;
	}
skip_buf_cache:
	if (buf->type == USERHEAP_BUF) {
		spin_lock(&fl->hlock);
		hlist_del_init(&buf->hn_rem);
		spin_unlock(&fl->hlock);
		buf->raddr = 0;
	}
	if (!IS_ERR_OR_NULL(buf->virt)) {
		int destVM[1] = {VMID_HLOS};
		int destVMperm[1] = {PERM_READ | PERM_WRITE | PERM_EXEC};

		VERIFY(err, fl->sctx != NULL);
		if (err)
			goto bail;
		if (fl->sctx->smmu.cb)
			buf->phys &= ~((uint64_t)fl->sctx->smmu.cb << 32);
		cid = fl->cid;
		VERIFY(err, VALID_FASTRPC_CID(cid));
		if (err) {
			err = -ECHRNG;
			ADSPRPC_ERR(
				"invalid channel 0x%zx set for session\n",
				cid);
			goto bail;
		}
		vmid = fl->apps->channel[cid].vmid;
		if (vmid) {
			int srcVM[2] = {VMID_HLOS, vmid};
			int hyp_err = 0;

			hyp_err = hyp_assign_phys(buf->phys,
				buf_page_size(buf->size),
				srcVM, 2, destVM, destVMperm, 1);
			if (hyp_err) {
				ADSPRPC_ERR(
					"rh hyp unassign failed with %d for phys 0x%llx, size %zu\n",
					hyp_err, buf->phys, buf->size);
			}
		}
		trace_fastrpc_dma_free(cid, buf->phys, buf->size);
		dma_free_attrs(fl->sctx->smmu.dev, buf->size, buf->virt,
					buf->phys, buf->dma_attr);
	}
bail:
	kfree(buf);
}

static void fastrpc_cached_buf_list_free(struct fastrpc_file *fl)
{
	struct fastrpc_buf *buf, *free;

	do {
		struct hlist_node *n;

		free = NULL;
		spin_lock(&fl->hlock);
		hlist_for_each_entry_safe(buf, n, &fl->cached_bufs, hn) {
			hlist_del_init(&buf->hn);
			fl->num_cached_buf--;
			free = buf;
			break;
		}
		spin_unlock(&fl->hlock);
		if (free)
			fastrpc_buf_free(free, 0);
	} while (free);
}

static void fastrpc_remote_buf_list_free(struct fastrpc_file *fl)
{
	struct fastrpc_buf *buf, *free;

	do {
		struct hlist_node *n;

		free = NULL;
		spin_lock(&fl->hlock);
		hlist_for_each_entry_safe(buf, n, &fl->remote_bufs, hn_rem) {
			free = buf;
			break;
		}
		spin_unlock(&fl->hlock);
		if (free)
			fastrpc_buf_free(free, 0);
	} while (free);
}

static void fastrpc_mmap_add(struct fastrpc_mmap *map)
{
	if (map->flags == ADSP_MMAP_HEAP_ADDR ||
				map->flags == ADSP_MMAP_REMOTE_HEAP_ADDR) {
		struct fastrpc_apps *me = &gfa;

		spin_lock(&me->hlock);
		hlist_add_head(&map->hn, &me->maps);
		spin_unlock(&me->hlock);
	} else {
		struct fastrpc_file *fl = map->fl;

		hlist_add_head(&map->hn, &fl->maps);
	}
}

static int fastrpc_mmap_find(struct fastrpc_file *fl, int fd,
		uintptr_t va, size_t len, int mflags, int refs,
		struct fastrpc_mmap **ppmap)
{
	struct fastrpc_apps *me = &gfa;
	struct fastrpc_mmap *match = NULL, *map = NULL;
	struct hlist_node *n;

	if ((va + len) < va)
		return -EFAULT;
	if (mflags == ADSP_MMAP_HEAP_ADDR ||
				 mflags == ADSP_MMAP_REMOTE_HEAP_ADDR) {
		spin_lock(&me->hlock);
		hlist_for_each_entry_safe(map, n, &me->maps, hn) {
			if (va >= map->va &&
				va + len <= map->va + map->len &&
				map->fd == fd) {
				if (refs) {
					if (map->refs + 1 == INT_MAX) {
						spin_unlock(&me->hlock);
						return -ETOOMANYREFS;
					}
					map->refs++;
				}
				match = map;
				break;
			}
		}
		spin_unlock(&me->hlock);
	} else {
		hlist_for_each_entry_safe(map, n, &fl->maps, hn) {
			if (va >= map->va &&
				va + len <= map->va + map->len &&
				map->fd == fd) {
				if (refs) {
					if (map->refs + 1 == INT_MAX)
						return -ETOOMANYREFS;
					map->refs++;
				}
				match = map;
				break;
			}
		}
	}
	if (match) {
		*ppmap = match;
		return 0;
	}
	return -ENXIO;
}

static int fastrpc_alloc_cma_memory(dma_addr_t *region_phys, void **vaddr,
				size_t size, unsigned long dma_attr)
{
	int err = 0;
	struct fastrpc_apps *me = &gfa;

	if (me->dev == NULL) {
		ADSPRPC_ERR(
			"failed to allocate CMA memory, device adsprpc-mem is not initialized\n");
		return -ENODEV;
	}
	VERIFY(err, size > 0 && size < me->max_size_limit);
	if (err) {
		err = -EFAULT;
		pr_err("adsprpc: %s: invalid allocation size 0x%zx\n",
			__func__, size);
		return err;
	}
	*vaddr = dma_alloc_attrs(me->dev, size, region_phys,
					GFP_KERNEL, dma_attr);
	if (IS_ERR_OR_NULL(*vaddr)) {
		ADSPRPC_ERR(
			"dma_alloc_attrs failed for device %s size 0x%zx dma_attr %lu, returned %ld\n",
			dev_name(me->dev), size, dma_attr, PTR_ERR(*vaddr));
		return -ENOBUFS;
	}
	return 0;
}

static int fastrpc_mmap_remove(struct fastrpc_file *fl, int fd, uintptr_t va,
			       size_t len, struct fastrpc_mmap **ppmap)
{
	struct fastrpc_mmap *match = NULL, *map;
	struct hlist_node *n;
	struct fastrpc_apps *me = &gfa;

	/*
	 * Search for a mapping by matching fd, remote address and length.
	 * For backward compatibility, search for a mapping by matching is
	 * limited to remote address and length when passed fd < 0.
	 */

	spin_lock(&me->hlock);
	hlist_for_each_entry_safe(map, n, &me->maps, hn) {
		if (map->refs == 1 && map->raddr == va &&
			map->raddr + map->len == va + len &&
			/* Remove map if not used in process initialization */
			!map->is_filemap) {
			match = map;
			hlist_del_init(&map->hn);
			break;
		}
	}
	spin_unlock(&me->hlock);
	if (match) {
		*ppmap = match;
		return 0;
	}
	hlist_for_each_entry_safe(map, n, &fl->maps, hn) {
		/* Remove if only one reference map and no context map */
		if (map->refs == 1 && !map->ctx_refs &&
			map->raddr == va && map->raddr + map->len == va + len &&
			/* Remove map if not used in process initialization */
			!map->is_filemap) {
			match = map;
			hlist_del_init(&map->hn);
			break;
		}
	}
	if (match) {
		*ppmap = match;
		return 0;
	}
	return -ETOOMANYREFS;
}

static void fastrpc_mmap_free(struct fastrpc_mmap *map, uint32_t flags)
{
	struct fastrpc_apps *me = &gfa;
	struct fastrpc_file *fl;
	int vmid, cid = -1, err = 0;
	struct fastrpc_session_ctx *sess;

	if (!map)
		return;
	fl = map->fl;
	if (fl && !(map->flags == ADSP_MMAP_HEAP_ADDR ||
				map->flags == ADSP_MMAP_REMOTE_HEAP_ADDR)) {
		cid = fl->cid;
		VERIFY(err, cid >= ADSP_DOMAIN_ID && cid < NUM_CHANNELS);
		if (err) {
			err = -ECHRNG;
			pr_err("adsprpc: ERROR:%s, Invalid channel id: %d, err:%d\n",
				__func__, cid, err);
			return;
		}
	}
	if (map->flags == ADSP_MMAP_HEAP_ADDR ||
				map->flags == ADSP_MMAP_REMOTE_HEAP_ADDR) {
		spin_lock(&me->hlock);
		map->refs--;
		if (!map->refs && !map->is_persistent && !map->ctx_refs)
			hlist_del_init(&map->hn);
		spin_unlock(&me->hlock);
		if (map->refs > 0) {
			ADSPRPC_WARN(
				"multiple references for remote heap size %zu va 0x%lx ref count is %d\n",
				map->size, map->va, map->refs);
			return;
		}
		if (map->is_persistent && map->in_use) {
			spin_lock(&me->hlock);
			map->in_use = false;
			spin_unlock(&me->hlock);
		}
	} else {
		map->refs--;
		if (!map->refs && !map->ctx_refs)
			hlist_del_init(&map->hn);
		if (map->refs > 0 && !flags)
			return;
	}
	if (map->flags == ADSP_MMAP_HEAP_ADDR ||
				map->flags == ADSP_MMAP_REMOTE_HEAP_ADDR) {

		if (me->dev == NULL) {
			ADSPRPC_ERR(
				"failed to free remote heap allocation, device is not initialized\n");
			return;
		}

		if (msm_minidump_enabled() && !map->is_persistent)
			err = fastrpc_minidump_remove_region(map);

		if (map->phys && !map->is_persistent) {
			trace_fastrpc_dma_free(-1, map->phys, map->size);
			dma_free_attrs(me->dev, map->size, (void *)map->va,
			(dma_addr_t)map->phys, (unsigned long)map->attr);
		}
	} else if (map->flags == FASTRPC_DMAHANDLE_NOMAP) {
		trace_fastrpc_dma_unmap(cid, map->phys, map->size);
		if (!IS_ERR_OR_NULL(map->table))
			dma_buf_unmap_attachment(map->attach, map->table,
					DMA_BIDIRECTIONAL);
		if (!IS_ERR_OR_NULL(map->attach))
			dma_buf_detach(map->buf, map->attach);
		if (!IS_ERR_OR_NULL(map->buf))
			dma_buf_put(map->buf);
	} else {
		int destVM[1] = {VMID_HLOS};
		int destVMperm[1] = {PERM_READ | PERM_WRITE | PERM_EXEC};

		if (map->secure)
			sess = fl->secsctx;
		else
			sess = fl->sctx;

		vmid = fl->apps->channel[cid].vmid;
		if (vmid && map->phys) {
			int hyp_err = 0;
			int srcVM[2] = {VMID_HLOS, vmid};

			hyp_err = hyp_assign_phys(map->phys,
				buf_page_size(map->size),
				srcVM, 2, destVM, destVMperm, 1);
			if (hyp_err) {
				ADSPRPC_ERR(
					"rh hyp unassign failed with %d for phys 0x%llx, size %zu\n",
					hyp_err, map->phys, map->size);
			}
		}
		trace_fastrpc_dma_unmap(cid, map->phys, map->size);
		if (!IS_ERR_OR_NULL(map->table))
			dma_buf_unmap_attachment(map->attach, map->table,
					DMA_BIDIRECTIONAL);
		if (!IS_ERR_OR_NULL(map->attach))
			dma_buf_detach(map->buf, map->attach);
		if (!IS_ERR_OR_NULL(map->buf))
			dma_buf_put(map->buf);
	}
	if (!map->is_persistent) {
		kfree(map);
	}
}

static int fastrpc_session_alloc(struct fastrpc_channel_ctx *chan, int secure,
					struct fastrpc_session_ctx **session);

static inline bool fastrpc_get_persistent_map(size_t len, struct fastrpc_mmap **pers_map)
{
	struct fastrpc_apps *me = &gfa;
	struct fastrpc_mmap *map = NULL;
	struct hlist_node *n = NULL;
	bool found = false;

	spin_lock(&me->hlock);
	hlist_for_each_entry_safe(map, n, &me->maps, hn) {
		if (len == map->len &&
			map->is_persistent && !map->in_use) {
			*pers_map = map;
			map->in_use = true;
			found = true;
			break;
		}
	}
	spin_unlock(&me->hlock);
	return found;
}

static int fastrpc_mmap_create(struct fastrpc_file *fl, int fd,
	unsigned int attr, uintptr_t va, size_t len, int mflags,
	struct fastrpc_mmap **ppmap)
{
	struct fastrpc_apps *me = &gfa;
	struct fastrpc_session_ctx *sess;
	struct fastrpc_apps *apps = fl->apps;
	int cid = fl->cid;
	struct fastrpc_channel_ctx *chan = NULL;
	struct fastrpc_mmap *map = NULL;
	dma_addr_t region_phys = 0;
	void *region_vaddr = NULL;
	unsigned long flags;
	int err = 0, vmid, sgl_index = 0;
	struct scatterlist *sgl = NULL;

	if (!fl) {
		err = -EBADF;
		goto bail;
	}
	cid = fl->cid;
	VERIFY(err, cid >= ADSP_DOMAIN_ID && cid < NUM_CHANNELS);
	if (err) {
		err = -ECHRNG;
		goto bail;
	}
	chan = &apps->channel[cid];

	if (!fastrpc_mmap_find(fl, fd, va, len, mflags, 1, ppmap))
		return 0;
	map = kzalloc(sizeof(*map), GFP_KERNEL);
	VERIFY(err, !IS_ERR_OR_NULL(map));
	if (err) {
		err = -ENOMEM;
		goto bail;
	}
	INIT_HLIST_NODE(&map->hn);
	map->flags = mflags;
	map->refs = 1;
	map->fl = fl;
	map->fd = fd;
	map->attr = attr;
	map->frpc_md_index = -1;
	map->is_filemap = false;
	map->ctx_refs = 0;
	ktime_get_real_ts64(&map->map_start_time);
	if (mflags == ADSP_MMAP_HEAP_ADDR ||
				mflags == ADSP_MMAP_REMOTE_HEAP_ADDR) {
		map->apps = me;
		map->fl = NULL;
		map->attr |= DMA_ATTR_SKIP_ZEROING | DMA_ATTR_NO_KERNEL_MAPPING;
		err = fastrpc_alloc_cma_memory(&region_phys, &region_vaddr,
					len, (unsigned long) map->attr);
		if (err)
			goto bail;
		trace_fastrpc_dma_alloc(fl->cid, (uint64_t)region_phys, len,
			(unsigned long)map->attr, mflags);
		map->phys = (uintptr_t)region_phys;
		map->size = len;
		map->va = (uintptr_t)region_vaddr;

		if (msm_minidump_enabled()) {
			err = fastrpc_minidump_add_region(map);
			if (err)
				goto bail;
		}
	} else if (mflags == FASTRPC_DMAHANDLE_NOMAP) {
		VERIFY(err, !IS_ERR_OR_NULL(map->buf = dma_buf_get(fd)));
		if (err) {
			ADSPRPC_ERR("dma_buf_get failed for fd %d ret %ld\n",
				fd, PTR_ERR(map->buf));
			err = -EBADFD;
			goto bail;
		}
		err = dma_buf_get_flags(map->buf, &flags);
		if (err) {
			ADSPRPC_ERR(
				"dma_buf_get_flags failed for fd %d ret %d\n",
				fd, err);
			err = -EFAULT;
			goto bail;
		}
		map->secure = flags & ION_FLAG_SECURE;
		map->uncached = 1;
		map->va = 0;
		map->phys = 0;

		VERIFY(err, !IS_ERR_OR_NULL(map->attach =
				dma_buf_attach(map->buf, me->dev)));
		if (err) {
			ADSPRPC_ERR(
				"dma_buf_attach for fd %d failed to map buffer on SMMU device %s ret %ld\n",
				fd, dev_name(me->dev), PTR_ERR(map->attach));
			err = -EFAULT;
			goto bail;
		}

		map->attach->dma_map_attrs |= DMA_ATTR_SKIP_CPU_SYNC;
		VERIFY(err, !IS_ERR_OR_NULL(map->table =
			dma_buf_map_attachment(map->attach,
				DMA_BIDIRECTIONAL)));
		if (err) {
			ADSPRPC_ERR(
				"dma_buf_map_attachment for fd %d failed on device %s ret %ld\n",
				fd, dev_name(me->dev), PTR_ERR(map->table));
			err = -EFAULT;
			goto bail;
		}
		VERIFY(err, map->table->nents == 1);
		if (err) {
			ADSPRPC_ERR(
				"multiple scatter-gather entries (%u) present for NOMAP fd %d\n",
				map->table->nents, fd);
			err = -EFAULT;
			goto bail;
		}
		map->phys = sg_dma_address(map->table->sgl);
		map->size = len;
		trace_fastrpc_dma_map(cid, fd, map->phys, map->size,
			len, mflags, map->attach->dma_map_attrs);
	} else {
		if (map->attr && (map->attr & FASTRPC_ATTR_KEEP_MAP)) {
			ADSPRPC_INFO("buffer mapped with persist attr 0x%x\n",
				(unsigned int)map->attr);
			map->refs = 2;
		}
		VERIFY(err, !IS_ERR_OR_NULL(map->buf = dma_buf_get(fd)));
		if (err) {
			ADSPRPC_ERR("dma_buf_get failed for fd %d ret %ld\n",
				fd, PTR_ERR(map->buf));
			err = -EBADFD;
			goto bail;
		}
		err = dma_buf_get_flags(map->buf, &flags);
		if (err) {
			ADSPRPC_ERR(
				"dma_buf_get_flags failed for fd %d ret %d\n",
				fd, err);
			err = -EFAULT;
			goto bail;
		}
		map->secure = flags & ION_FLAG_SECURE;
		if (map->secure) {
			if (!fl->secsctx)
				err = fastrpc_session_alloc(chan, 1,
							&fl->secsctx);
			if (err) {
				ADSPRPC_ERR(
					"fastrpc_session_alloc failed for fd %d ret %d\n",
					fd, err);
				err = -ENOSR;
				goto bail;
			}
		}
		if (map->secure)
			sess = fl->secsctx;
		else
			sess = fl->sctx;

		VERIFY(err, !IS_ERR_OR_NULL(sess));
		if (err) {
			ADSPRPC_ERR(
				"session is invalid for fd %d, secure flag %d\n",
				fd, map->secure);
			goto bail;
		}

		map->uncached = !(flags & ION_FLAG_CACHED);
		if (map->attr & FASTRPC_ATTR_NOVA && !sess->smmu.coherent)
			map->uncached = 1;

		VERIFY(err, !IS_ERR_OR_NULL(map->attach =
				dma_buf_attach(map->buf, sess->smmu.dev)));
		if (err) {
			ADSPRPC_ERR(
				"dma_buf_attach for fd %d failed to map buffer on SMMU device %s ret %ld\n",
				fd, dev_name(sess->smmu.dev),
				PTR_ERR(map->attach));
			err = -EFAULT;
			goto bail;
		}

		map->attach->dma_map_attrs |= DMA_ATTR_DELAYED_UNMAP;
		map->attach->dma_map_attrs |= DMA_ATTR_EXEC_MAPPING;

		/*
		 * Skip CPU sync if IO Cohernecy is not supported
		 * or if it is supported but buffer is uncached
		 */
		if ((sess->smmu.coherent && map->uncached) ||
			(!sess->smmu.coherent))
			map->attach->dma_map_attrs |= DMA_ATTR_SKIP_CPU_SYNC;

		VERIFY(err, !IS_ERR_OR_NULL(map->table =
			dma_buf_map_attachment(map->attach,
				DMA_BIDIRECTIONAL)));
		if (err) {
			ADSPRPC_ERR(
				"dma_buf_map_attachment for fd %d failed on device %s ret %ld\n",
				fd, dev_name(sess->smmu.dev),
				PTR_ERR(map->table));
			err = -EFAULT;
			goto bail;
		}
		if (!sess->smmu.enabled) {
			VERIFY(err, map->table->nents == 1);
			if (err) {
				ADSPRPC_ERR(
					"multiple scatter-gather entries (%u) present for fd %d mapped on SMMU disabled device\n",
					map->table->nents, fd);
				err = -EFAULT;
				goto bail;
			}
		}
		map->phys = sg_dma_address(map->table->sgl);

		if (sess->smmu.cb) {
			map->phys += ((uint64_t)sess->smmu.cb << 32);
			for_each_sg(map->table->sgl, sgl, map->table->nents,
				sgl_index)
				map->size += sg_dma_len(sgl);
		} else {
			map->size = buf_page_size(len);
		}
		trace_fastrpc_dma_map(cid, fd, map->phys, map->size,
			len, mflags, map->attach->dma_map_attrs);

		VERIFY(err, map->size >= len && map->size < me->max_size_limit);
		if (err) {
			err = -EFAULT;
			pr_err("adsprpc: %s: invalid map size 0x%zx len 0x%zx\n",
				__func__, map->size, len);
			goto bail;
		}

		vmid = fl->apps->channel[cid].vmid;
		if (vmid) {
			int srcVM[1] = {VMID_HLOS};
			int destVM[2] = {VMID_HLOS, vmid};
			int destVMperm[2] = {PERM_READ | PERM_WRITE,
					PERM_READ | PERM_WRITE | PERM_EXEC};

			err = hyp_assign_phys(map->phys,
					buf_page_size(map->size),
					srcVM, 1, destVM, destVMperm, 2);
			if (err) {
				ADSPRPC_ERR(
					"rh hyp assign failed with %d for phys 0x%llx, size %zu\n",
					err, map->phys, map->size);
				err = -EADDRNOTAVAIL;
				goto bail;
			}
		}
		map->va = va;
	}
	map->len = len;

	fastrpc_mmap_add(map);
	*ppmap = map;

bail:
	if (map)
		ktime_get_real_ts64(&map->map_end_time);
	if (err && map)
		fastrpc_mmap_free(map, 0);
	return err;
}

static inline bool fastrpc_get_cached_buf(struct fastrpc_file *fl,
		size_t size, int buf_type, struct fastrpc_buf **obuf)
{
	bool found = false;
	struct fastrpc_buf *buf = NULL, *fr = NULL;
	struct hlist_node *n = NULL;

	if (buf_type == USERHEAP_BUF)
		goto bail;

	/* find the smallest buffer that fits in the cache */
	spin_lock(&fl->hlock);
	hlist_for_each_entry_safe(buf, n, &fl->cached_bufs, hn) {
		if (buf->size >= size && (!fr || fr->size > buf->size))
			fr = buf;
	}
	if (fr) {
		hlist_del_init(&fr->hn);
		fl->num_cached_buf--;
	}
	spin_unlock(&fl->hlock);
	if (fr) {
		fr->type = buf_type;
		*obuf = fr;
		found = true;
	}
bail:
	return found;
}

static inline bool fastrpc_get_persistent_buf(struct fastrpc_file *fl,
		size_t size, int buf_type, struct fastrpc_buf **obuf)
{
	unsigned int i = 0;
	bool found = false;
	struct fastrpc_buf *buf = NULL;

	spin_lock(&fl->hlock);
	if (!fl->num_pers_hdrs)
		goto bail;

	/*
	 * Persistent header buffer can be used only if
	 * metadata length is less than 1 page size.
	 */
	if (buf_type != METADATA_BUF || size > PAGE_SIZE)
		goto bail;

	for (i = 0; i < fl->num_pers_hdrs; i++) {
		buf = &fl->hdr_bufs[i];
		/* If buffer not in use, then assign it for requested alloc */
		if (!buf->in_use) {
			buf->in_use = true;
			*obuf = buf;
			found = true;
			break;
		}
	}
bail:
	spin_unlock(&fl->hlock);
	return found;
}

static int fastrpc_buf_alloc(struct fastrpc_file *fl, size_t size,
			unsigned long dma_attr, uint32_t rflags,
			int buf_type, struct fastrpc_buf **obuf)
{
	int err = 0, vmid, cid = -1;
	struct fastrpc_apps *me = &gfa;
	struct fastrpc_buf *buf = NULL;

	VERIFY(err, size > 0 && size < me->max_size_limit);
	if (err) {
		err = -EFAULT;
		pr_err("adsprpc: %s: invalid allocation size 0x%zx\n",
			__func__, size);
		goto bail;
	}

	VERIFY(err, fl && fl->sctx != NULL);
	if (err) {
		err = -EBADR;
		goto bail;
	}
	cid = fl->cid;
	VERIFY(err, VALID_FASTRPC_CID(cid));
	if (err) {
		err = -ECHRNG;
		goto bail;
	}

	VERIFY(err, size > 0 && fl->sctx->smmu.dev);
	if (err) {
		err = (fl->sctx->smmu.dev == NULL) ? -ENODEV : err;
		goto bail;
	}
	if (fastrpc_get_persistent_buf(fl, size, buf_type, obuf))
		return err;
	if (fastrpc_get_cached_buf(fl, size, buf_type, obuf))
		return err;

	/* If unable to get persistent or cached buf, allocate new buffer */
	VERIFY(err, NULL != (buf = kzalloc(sizeof(*buf), GFP_KERNEL)));
	if (err) {
		err = -ENOMEM;
		goto bail;
	}
	INIT_HLIST_NODE(&buf->hn);
	buf->fl = fl;
	buf->virt = NULL;
	buf->phys = 0;
	buf->size = size;
	buf->dma_attr = dma_attr;
	buf->flags = rflags;
	buf->raddr = 0;
	buf->type = buf_type;
	ktime_get_real_ts64(&buf->buf_start_time);

	buf->virt = dma_alloc_attrs(fl->sctx->smmu.dev, buf->size,
						(dma_addr_t *)&buf->phys,
						GFP_KERNEL, buf->dma_attr);
	if (IS_ERR_OR_NULL(buf->virt)) {
		/* free cache and retry */
		fastrpc_cached_buf_list_free(fl);
		buf->virt = dma_alloc_attrs(fl->sctx->smmu.dev, buf->size,
					(dma_addr_t *)&buf->phys, GFP_KERNEL,
					buf->dma_attr);
		VERIFY(err, !IS_ERR_OR_NULL(buf->virt));
	}
	if (err) {
		ADSPRPC_ERR(
			"dma_alloc_attrs failed for size 0x%zx, returned %pK\n",
			size, buf->virt);
		err = -ENOBUFS;
		goto bail;
	}
	if (fl->sctx->smmu.cb)
		buf->phys += ((uint64_t)fl->sctx->smmu.cb << 32);
	trace_fastrpc_dma_alloc(cid, buf->phys, size,
		dma_attr, (int)rflags);

	vmid = fl->apps->channel[cid].vmid;
	if (vmid) {
		int srcVM[1] = {VMID_HLOS};
		int destVM[2] = {VMID_HLOS, vmid};
		int destVMperm[2] = {PERM_READ | PERM_WRITE,
					PERM_READ | PERM_WRITE | PERM_EXEC};

		err = hyp_assign_phys(buf->phys, buf_page_size(size),
			srcVM, 1, destVM, destVMperm, 2);
		if (err) {
			ADSPRPC_DEBUG(
				"rh hyp assign failed with %d for phys 0x%llx, size %zu\n",
				err, buf->phys, size);
			err = -EADDRNOTAVAIL;
			goto bail;
		}
	}

	if (buf_type == USERHEAP_BUF) {
		INIT_HLIST_NODE(&buf->hn_rem);
		spin_lock(&fl->hlock);
		hlist_add_head(&buf->hn_rem, &fl->remote_bufs);
		spin_unlock(&fl->hlock);
	}
	*obuf = buf;
 bail:
	if (buf)
		ktime_get_real_ts64(&buf->buf_end_time);
	if (err && buf)
		fastrpc_buf_free(buf, 0);
	return err;
}


static int context_restore_interrupted(struct fastrpc_file *fl,
				struct fastrpc_ioctl_invoke_async *inv,
				struct smq_invoke_ctx **po)
{
	int err = 0;
	struct smq_invoke_ctx *ctx = NULL, *ictx = NULL;
	struct hlist_node *n;
	struct fastrpc_ioctl_invoke *invoke = &inv->inv;

	spin_lock(&fl->hlock);
	hlist_for_each_entry_safe(ictx, n, &fl->clst.interrupted, hn) {
		if (ictx->pid == current->pid) {
			if (invoke->sc != ictx->sc || ictx->fl != fl) {
				err = -EINVAL;
				ictx->sc_interrupted = invoke->sc;
				ictx->fl_interrupted = fl;
				ictx->handle_interrupted = invoke->handle;
				ADSPRPC_ERR(
					"interrupted sc (0x%x) or fl (%pK) does not match with invoke sc (0x%x) or fl (%pK)\n",
					ictx->sc, ictx->fl, invoke->sc, fl);
			}
			else {
				ctx = ictx;
				hlist_del_init(&ctx->hn);
				hlist_add_head(&ctx->hn, &fl->clst.pending);
			}
			break;
		}
	}
	spin_unlock(&fl->hlock);
	if (ctx)
		*po = ctx;
	return err;
}

static unsigned int sorted_lists_intersection(unsigned int *listA,
		unsigned int lenA, unsigned int *listB, unsigned int lenB)
{
	unsigned int i = 0, j = 0;

	while (i < lenA && j < lenB) {
		if (listA[i] < listB[j])
			i++;
		else if (listA[i] > listB[j])
			j++;
		else
			return listA[i];
	}
	return 0;
}

#define CMP(aa, bb) ((aa) == (bb) ? 0 : (aa) < (bb) ? -1 : 1)

static int uint_cmp_func(const void *p1, const void *p2)
{
	unsigned int a1 = *((unsigned int *)p1);
	unsigned int a2 = *((unsigned int *)p2);

	return CMP(a1, a2);
}

static int overlap_ptr_cmp(const void *a, const void *b)
{
	struct overlap *pa = *((struct overlap **)a);
	struct overlap *pb = *((struct overlap **)b);
	/* sort with lowest starting buffer first */
	int st = CMP(pa->start, pb->start);
	/* sort with highest ending buffer first */
	int ed = CMP(pb->end, pa->end);
	return st == 0 ? ed : st;
}

static int context_build_overlap(struct smq_invoke_ctx *ctx)
{
	int i, err = 0;
	remote_arg_t *lpra = ctx->lpra;
	int inbufs = REMOTE_SCALARS_INBUFS(ctx->sc);
	int outbufs = REMOTE_SCALARS_OUTBUFS(ctx->sc);
	int nbufs = inbufs + outbufs;
	struct overlap max;

	for (i = 0; i < nbufs; ++i) {
		ctx->overs[i].start = (uintptr_t)lpra[i].buf.pv;
		ctx->overs[i].end = ctx->overs[i].start + lpra[i].buf.len;
		if (lpra[i].buf.len) {
			VERIFY(err, ctx->overs[i].end > ctx->overs[i].start);
			if (err) {
				err = -EFAULT;
				ADSPRPC_ERR(
					"Invalid address 0x%llx and size %zu\n",
					(uintptr_t)lpra[i].buf.pv,
					lpra[i].buf.len);
				goto bail;
			}
		}
		ctx->overs[i].raix = i;
		ctx->overps[i] = &ctx->overs[i];
	}
	sort(ctx->overps, nbufs, sizeof(*ctx->overps), overlap_ptr_cmp, NULL);
	max.start = 0;
	max.end = 0;
	for (i = 0; i < nbufs; ++i) {
		if (ctx->overps[i]->start < max.end) {
			ctx->overps[i]->mstart = max.end;
			ctx->overps[i]->mend = ctx->overps[i]->end;
			ctx->overps[i]->offset = max.end -
				ctx->overps[i]->start;
			if (ctx->overps[i]->end > max.end) {
				max.end = ctx->overps[i]->end;
			} else {
				if ((max.raix < inbufs &&
					ctx->overps[i]->raix + 1 > inbufs) ||
					(ctx->overps[i]->raix < inbufs &&
					max.raix + 1 > inbufs))
					ctx->overps[i]->do_cmo = 1;
				ctx->overps[i]->mend = 0;
				ctx->overps[i]->mstart = 0;
			}
		} else  {
			ctx->overps[i]->mend = ctx->overps[i]->end;
			ctx->overps[i]->mstart = ctx->overps[i]->start;
			ctx->overps[i]->offset = 0;
			max = *ctx->overps[i];
		}
	}
bail:
	return err;
}

#define K_COPY_FROM_USER(err, kernel, dst, src, size) \
	do {\
		if (!(kernel))\
			err = copy_from_user((dst),\
			(void const __user *)(src),\
			(size));\
		else\
			memmove((dst), (src), (size));\
	} while (0)

#define K_COPY_TO_USER(err, kernel, dst, src, size) \
	do {\
		if (!(kernel))\
			err = copy_to_user((void __user *)(dst),\
			(src), (size));\
		else\
			memmove((dst), (src), (size));\
	} while (0)


static void context_free(struct smq_invoke_ctx *ctx);

static int context_alloc(struct fastrpc_file *fl, uint32_t kernel,
			 struct fastrpc_ioctl_invoke_async *invokefd,
			 struct smq_invoke_ctx **po)
{
	struct fastrpc_apps *me = &gfa;
	int err = 0, bufs, ii, size = 0, cid = fl->cid;
	struct smq_invoke_ctx *ctx = NULL;
	struct fastrpc_ctx_lst *clst = &fl->clst;
	struct fastrpc_ioctl_invoke *invoke = &invokefd->inv;
	struct fastrpc_channel_ctx *chan = NULL;
	unsigned long irq_flags = 0;

	spin_lock(&fl->hlock);
	if (fl->clst.num_active_ctxs > MAX_PENDING_CTX_PER_SESSION &&
		!(kernel || invoke->handle < FASTRPC_STATIC_HANDLE_MAX)) {
		err = -EDQUOT;
		spin_unlock(&fl->hlock);
		goto bail;
	}
	spin_unlock(&fl->hlock);
	bufs = REMOTE_SCALARS_LENGTH(invoke->sc);
	size = bufs * sizeof(*ctx->lpra) + bufs * sizeof(*ctx->maps) +
		sizeof(*ctx->fds) * (bufs) +
		sizeof(*ctx->attrs) * (bufs) +
		sizeof(*ctx->overs) * (bufs) +
		sizeof(*ctx->overps) * (bufs);

	VERIFY(err, NULL != (ctx = kzalloc(sizeof(*ctx) + size, GFP_KERNEL)));
	if (err) {
		err = -ENOMEM;
		goto bail;
	}

	INIT_HLIST_NODE(&ctx->hn);
	INIT_LIST_HEAD(&ctx->asyncn);
	hlist_add_fake(&ctx->hn);
	ctx->fl = fl;
	ctx->maps = (struct fastrpc_mmap **)(&ctx[1]);
	ctx->lpra = (remote_arg_t *)(&ctx->maps[bufs]);
	ctx->fds = (int *)(&ctx->lpra[bufs]);
	ctx->attrs = (unsigned int *)(&ctx->fds[bufs]);
	ctx->overs = (struct overlap *)(&ctx->attrs[bufs]);
	ctx->overps = (struct overlap **)(&ctx->overs[bufs]);

	K_COPY_FROM_USER(err, kernel, (void *)ctx->lpra, invoke->pra,
					bufs * sizeof(*ctx->lpra));
	if (err) {
		ADSPRPC_ERR(
			"copy from user failed with %d for remote arguments list\n",
			err);
		err = -EFAULT;
		goto bail;
	}

	if (invokefd->fds) {
		K_COPY_FROM_USER(err, kernel, ctx->fds, invokefd->fds,
						bufs * sizeof(*ctx->fds));
		if (err) {
			ADSPRPC_ERR(
				"copy from user failed with %d for fd list\n",
				err);
			err = -EFAULT;
			goto bail;
		}
	} else {
		ctx->fds = NULL;
	}
	if (invokefd->attrs) {
		K_COPY_FROM_USER(err, kernel, ctx->attrs, invokefd->attrs,
						bufs * sizeof(*ctx->attrs));
		if (err) {
			ADSPRPC_ERR(
				"copy from user failed with %d for attribute list\n",
				err);
			err = -EFAULT;
			goto bail;
		}
	}
	ctx->crc = (uint32_t *)invokefd->crc;
	ctx->perf_dsp = (uint64_t *)invokefd->perf_dsp;
	ctx->perf_kernel = (uint64_t *)invokefd->perf_kernel;
	ctx->handle = invoke->handle;
	ctx->sc = invoke->sc;
	if (bufs) {
		VERIFY(err, 0 == (err = context_build_overlap(ctx)));
		if (err)
			goto bail;
	}
	ctx->retval = -1;
	ctx->pid = current->pid;
	ctx->tgid = fl->tgid;
	init_completion(&ctx->work);
	ctx->magic = FASTRPC_CTX_MAGIC;
	ctx->rsp_flags = NORMAL_RESPONSE;
	ctx->is_work_done = false;
	ctx->copybuf = NULL;
	ctx->is_early_wakeup = false;

	if (ctx->fl->profile) {
		ctx->perf = kzalloc(sizeof(*(ctx->perf)), GFP_KERNEL);
		VERIFY(err, !IS_ERR_OR_NULL(ctx->perf));
		if (err) {
			kfree(ctx->perf);
			err = -ENOMEM;
			goto bail;
		}
		memset(ctx->perf, 0, sizeof(*(ctx->perf)));
		ctx->perf->tid = fl->tgid;
	}
	if (invokefd->job) {
		K_COPY_FROM_USER(err, kernel, &ctx->asyncjob, invokefd->job,
						sizeof(ctx->asyncjob));
		if (err)
			goto bail;
	}
	VERIFY(err, VALID_FASTRPC_CID(cid));
	if (err) {
		err = -ECHRNG;
		goto bail;
	}
	chan = &me->channel[cid];

	spin_lock_irqsave(&chan->ctxlock, irq_flags);
	me->jobid[cid]++;
	for (ii = ((kernel || ctx->handle < FASTRPC_STATIC_HANDLE_MAX)
				? 0 : NUM_KERNEL_AND_STATIC_ONLY_CONTEXTS);
				ii < FASTRPC_CTX_MAX; ii++) {
		if (!chan->ctxtable[ii]) {
			chan->ctxtable[ii] = ctx;
			ctx->ctxid = (me->jobid[cid] << FASTRPC_CTX_JOBID_POS)
			  | (ii << FASTRPC_CTX_TABLE_IDX_POS)
			  | ((ctx->asyncjob.isasyncjob &&
			  FASTRPC_ASYNC_JOB_MASK) << FASTRPC_CTX_JOB_TYPE_POS);
			break;
		}
	}
	spin_unlock_irqrestore(&chan->ctxlock, irq_flags);
	VERIFY(err, ii < FASTRPC_CTX_MAX);
	if (err) {
		ADSPRPC_ERR(
			"adsprpc: out of context table entries for handle 0x%x, sc 0x%x\n",
			ctx->handle, ctx->sc);
		err = -ENOKEY;
		goto bail;
	}
	spin_lock(&fl->hlock);
	hlist_add_head(&ctx->hn, &clst->pending);
	clst->num_active_ctxs++;
	spin_unlock(&fl->hlock);

	trace_fastrpc_context_alloc((uint64_t)ctx,
		ctx->ctxid | fl->pd, ctx->handle, ctx->sc);
	*po = ctx;
bail:
	if (ctx && err)
		context_free(ctx);
	return err;
}

static void context_save_interrupted(struct smq_invoke_ctx *ctx)
{
	struct fastrpc_ctx_lst *clst = &ctx->fl->clst;

	spin_lock(&ctx->fl->hlock);
	hlist_del_init(&ctx->hn);
	hlist_add_head(&ctx->hn, &clst->interrupted);
	spin_unlock(&ctx->fl->hlock);
}

static void context_free(struct smq_invoke_ctx *ctx)
{
	uint32_t i = 0;
	struct fastrpc_apps *me = &gfa;
	int nbufs = REMOTE_SCALARS_INBUFS(ctx->sc) +
		    REMOTE_SCALARS_OUTBUFS(ctx->sc);
	int cid = ctx->fl->cid;
	struct fastrpc_channel_ctx *chan = &me->channel[cid];
	unsigned long irq_flags = 0;

	i = (uint32_t)GET_TABLE_IDX_FROM_CTXID(ctx->ctxid);

	spin_lock_irqsave(&chan->ctxlock, irq_flags);
	if (i < FASTRPC_CTX_MAX && chan->ctxtable[i] == ctx) {
		chan->ctxtable[i] = NULL;
	} else {
		for (i = 0; i < FASTRPC_CTX_MAX; i++) {
			if (chan->ctxtable[i] == ctx) {
				chan->ctxtable[i] = NULL;
				break;
			}
		}
	}
	spin_unlock_irqrestore(&chan->ctxlock, irq_flags);

	spin_lock(&ctx->fl->hlock);
	if (!hlist_unhashed(&ctx->hn)) {
		hlist_del_init(&ctx->hn);
		ctx->fl->clst.num_active_ctxs--;
	}
	spin_unlock(&ctx->fl->hlock);

	mutex_lock(&ctx->fl->map_mutex);
	for (i = 0; i < nbufs; ++i) {
		if (ctx->maps[i] && ctx->maps[i]->ctx_refs)
			ctx->maps[i]->ctx_refs--;
		fastrpc_mmap_free(ctx->maps[i], 0);
	}
	mutex_unlock(&ctx->fl->map_mutex);

	fastrpc_buf_free(ctx->buf, 1);
	if (ctx->copybuf != ctx->buf)
		fastrpc_buf_free(ctx->copybuf, 1);
	kfree(ctx->lrpra);
	ctx->lrpra = NULL;
	ctx->magic = 0;
	ctx->ctxid = 0;
	if (ctx->fl->profile)
		kfree(ctx->perf);

	trace_fastrpc_context_free((uint64_t)ctx,
		ctx->msg.invoke.header.ctx, ctx->handle, ctx->sc);
	kfree(ctx);
}

static void fastrpc_queue_completed_async_job(struct smq_invoke_ctx *ctx)
{
	struct fastrpc_file *fl = ctx->fl;
	unsigned long flags;

	spin_lock_irqsave(&fl->aqlock, flags);
	if (ctx->is_early_wakeup)
		goto bail;
	list_add_tail(&ctx->asyncn, &fl->clst.async_queue);
	atomic_add(1, &fl->async_queue_job_count);
	ctx->is_early_wakeup = true;
	wake_up_interruptible(&fl->async_wait_queue);
bail:
	spin_unlock_irqrestore(&fl->aqlock, flags);
}

static void context_notify_user(struct smq_invoke_ctx *ctx,
		int retval, uint32_t rsp_flags, uint32_t early_wake_time)
{
	fastrpc_pm_awake(ctx->fl, gcinfo[ctx->fl->cid].secure);
	ctx->retval = retval;
	ctx->rsp_flags = (enum fastrpc_response_flags)rsp_flags;
	trace_fastrpc_context_complete(ctx->fl->cid, (uint64_t)ctx, retval,
			ctx->msg.invoke.header.ctx, ctx->handle, ctx->sc);
	switch (rsp_flags) {
	case NORMAL_RESPONSE:
	case COMPLETE_SIGNAL:
		/* normal and complete response with return value */
		ctx->is_work_done = true;
		if (ctx->asyncjob.isasyncjob)
			fastrpc_queue_completed_async_job(ctx);
		complete(&ctx->work);
		break;
	case USER_EARLY_SIGNAL:
		/* user hint of approximate time of completion */
		ctx->early_wake_time = early_wake_time;
		if (ctx->asyncjob.isasyncjob)
			break;
	case EARLY_RESPONSE:
		/* rpc framework early response with return value */
		if (ctx->asyncjob.isasyncjob)
			fastrpc_queue_completed_async_job(ctx);
		else
			complete(&ctx->work);
		break;
	default:
		break;
	}
}

static void fastrpc_notify_users(struct fastrpc_file *me)
{
	struct smq_invoke_ctx *ictx;
	struct hlist_node *n;

	spin_lock(&me->hlock);
	hlist_for_each_entry_safe(ictx, n, &me->clst.pending, hn) {
		ictx->is_work_done = true;
		ictx->retval = -ECONNRESET;
		trace_fastrpc_context_complete(me->cid, (uint64_t)ictx,
			ictx->retval, ictx->msg.invoke.header.ctx,
			ictx->handle, ictx->sc);
		if (ictx->asyncjob.isasyncjob)
			fastrpc_queue_completed_async_job(ictx);
		else
			complete(&ictx->work);
	}
	hlist_for_each_entry_safe(ictx, n, &me->clst.interrupted, hn) {
		ictx->is_work_done = true;
		ictx->retval = -ECONNRESET;
		trace_fastrpc_context_complete(me->cid, (uint64_t)ictx,
			ictx->retval, ictx->msg.invoke.header.ctx,
			ictx->handle, ictx->sc);
		complete(&ictx->work);
	}
	spin_unlock(&me->hlock);
}


static void fastrpc_notify_users_staticpd_pdr(struct fastrpc_file *me)
{
	struct smq_invoke_ctx *ictx;
	struct hlist_node *n;

	spin_lock(&me->hlock);
	hlist_for_each_entry_safe(ictx, n, &me->clst.pending, hn) {
		if (ictx->msg.pid) {
			ictx->is_work_done = true;
			ictx->retval = -ECONNRESET;
			trace_fastrpc_context_complete(me->cid, (uint64_t)ictx,
				ictx->retval, ictx->msg.invoke.header.ctx,
				ictx->handle, ictx->sc);
			if (ictx->asyncjob.isasyncjob)
				fastrpc_queue_completed_async_job(ictx);
			else
				complete(&ictx->work);
		}
	}
	hlist_for_each_entry_safe(ictx, n, &me->clst.interrupted, hn) {
		if (ictx->msg.pid) {
			ictx->is_work_done = true;
			ictx->retval = -ECONNRESET;
			trace_fastrpc_context_complete(me->cid, (uint64_t)ictx,
				ictx->retval, ictx->msg.invoke.header.ctx,
				ictx->handle, ictx->sc);
			complete(&ictx->work);
		}
	}
	spin_unlock(&me->hlock);
}

static void fastrpc_ramdump_collection(int cid)
{
	struct fastrpc_file *fl = NULL;
	struct hlist_node *n = NULL;
	struct fastrpc_apps *me = &gfa;
	struct fastrpc_channel_ctx *chan = &me->channel[cid];
	struct ramdump_segment ramdump_entry;
	struct fastrpc_buf *buf = NULL;
	int ret = 0;
	char ramdump_name[RAMDUMP_NAME_MAX_LENGTH];

	spin_lock(&me->hlock);
	hlist_for_each_entry_safe(fl, n, &me->drivers, hn) {
	/*
	 * On SSR call back, dumps collected only when process is initialized,
	 * And, process is not cleaned on DSP to avoid UAF for init_mem.
	 */
		if (fl->cid == cid && fl->init_mem &&
			fl->file_close < FASTRPC_PROCESS_DSP_EXIT_COMPLETE &&
			fl->dsp_proc_init) {
			hlist_add_head(&fl->init_mem->hn_init, &chan->initmems);
			fl->is_ramdump_pend = true;
		}
	}
	spin_unlock(&me->hlock);

	if (chan->rh_dump_dev) {
		hlist_for_each_entry_safe(buf, n, &chan->initmems, hn_init) {
			memset(&ramdump_entry, 0, sizeof(ramdump_entry));
			ramdump_entry.address = buf->phys;
			ramdump_entry.v_address = (void __iomem *)buf->virt;
			ramdump_entry.size = buf->size;
			if (buf->fl) {
				scnprintf(ramdump_name, ARRAY_SIZE(ramdump_name),
					"%s_%x_", gcinfo[cid].subsys, buf->fl->tgid);
				ramdump_entry.name = ramdump_name;
			}
			ret = fastrpc_elf_ramdump(chan->rh_dump_dev, &ramdump_entry);
			if (ret < 0)
				ADSPRPC_ERR("adsprpc: %s: unable to dump PD memory (err %d)\n",
					__func__, ret);
			if (buf->fl)
				complete(&buf->fl->work);
			hlist_del_init(&buf->hn_init);
		}
	}
}

static void fastrpc_notify_drivers(struct fastrpc_apps *me, int cid)
{
	struct fastrpc_file *fl;
	struct hlist_node *n;

	spin_lock(&me->hlock);
	hlist_for_each_entry_safe(fl, n, &me->drivers, hn) {
		if (fl->cid == cid)
			fastrpc_notify_users(fl);
	}
	spin_unlock(&me->hlock);
}

static void fastrpc_notify_pdr_drivers(struct fastrpc_apps *me,
		char *servloc_name)
{
	struct fastrpc_file *fl;
	struct hlist_node *n;

	spin_lock(&me->hlock);
	hlist_for_each_entry_safe(fl, n, &me->drivers, hn) {
		if (fl->servloc_name && !strcmp(servloc_name, fl->servloc_name))
			fastrpc_notify_users_staticpd_pdr(fl);
	}
	spin_unlock(&me->hlock);
}

static void context_list_ctor(struct fastrpc_ctx_lst *me)
{
	INIT_HLIST_HEAD(&me->interrupted);
	INIT_HLIST_HEAD(&me->pending);
	me->num_active_ctxs = 0;
	INIT_LIST_HEAD(&me->async_queue);
}

static void fastrpc_context_list_dtor(struct fastrpc_file *fl)
{
	struct fastrpc_ctx_lst *clst = &fl->clst;
	struct smq_invoke_ctx *ictx = NULL, *ctxfree;
	struct hlist_node *n;

	do {
		ctxfree = NULL;
		spin_lock(&fl->hlock);
		hlist_for_each_entry_safe(ictx, n, &clst->interrupted, hn) {
			hlist_del_init(&ictx->hn);
			clst->num_active_ctxs--;
			ctxfree = ictx;
			break;
		}
		spin_unlock(&fl->hlock);
		if (ctxfree)
			context_free(ctxfree);
	} while (ctxfree);
	do {
		ctxfree = NULL;
		spin_lock(&fl->hlock);
		hlist_for_each_entry_safe(ictx, n, &clst->pending, hn) {
			hlist_del_init(&ictx->hn);
			clst->num_active_ctxs--;
			ctxfree = ictx;
			break;
		}
		spin_unlock(&fl->hlock);
		if (ctxfree)
			context_free(ctxfree);
	} while (ctxfree);
}

static int fastrpc_file_free(struct fastrpc_file *fl);
static void fastrpc_file_list_dtor(struct fastrpc_apps *me)
{
	struct fastrpc_file *fl, *free;
	struct hlist_node *n;

	do {
		free = NULL;
		spin_lock(&me->hlock);
		hlist_for_each_entry_safe(fl, n, &me->drivers, hn) {
			hlist_del_init(&fl->hn);
			free = fl;
			break;
		}
		spin_unlock(&me->hlock);
		if (free)
			fastrpc_file_free(free);
	} while (free);
}

static int get_args(uint32_t kernel, struct smq_invoke_ctx *ctx)
{
	remote_arg64_t *rpra, *lrpra;
	remote_arg_t *lpra = ctx->lpra;
	struct smq_invoke_buf *list;
	struct smq_phy_page *pages, *ipage;
	uint32_t sc = ctx->sc;
	int inbufs = REMOTE_SCALARS_INBUFS(sc);
	int outbufs = REMOTE_SCALARS_OUTBUFS(sc);
	int handles, bufs = inbufs + outbufs;
	uintptr_t args = 0;
	size_t rlen = 0, copylen = 0, metalen = 0, lrpralen = 0, templen = 0;
	size_t totallen = 0; //header and non ion copy buf len
	int i, oix;
	int err = 0, j = 0;
	int mflags = 0;
	uint64_t *fdlist = NULL;
	uint32_t *crclist = NULL;
	uint32_t early_hint;
	uint64_t *perf_counter = NULL;

	if (ctx->fl->profile)
		perf_counter = (uint64_t *)ctx->perf + PERF_COUNT;

	/* calculate size of the metadata */
	rpra = NULL;
	lrpra = NULL;
	list = smq_invoke_buf_start(rpra, sc);
	pages = smq_phy_page_start(sc, list);
	ipage = pages;

	PERF(ctx->fl->profile, GET_COUNTER(perf_counter, PERF_MAP),
	for (i = 0; i < bufs; ++i) {
		uintptr_t buf = (uintptr_t)lpra[i].buf.pv;
		size_t len = lpra[i].buf.len;

		mutex_lock(&ctx->fl->map_mutex);
		if (ctx->fds && (ctx->fds[i] != -1))
			err = fastrpc_mmap_create(ctx->fl, ctx->fds[i],
					ctx->attrs[i], buf, len,
					mflags, &ctx->maps[i]);
		if (ctx->maps[i])
			ctx->maps[i]->ctx_refs++;
		mutex_unlock(&ctx->fl->map_mutex);
		if (err)
			goto bail;
		ipage += 1;
	}
	PERF_END);
	handles = REMOTE_SCALARS_INHANDLES(sc) + REMOTE_SCALARS_OUTHANDLES(sc);
	mutex_lock(&ctx->fl->map_mutex);
	for (i = bufs; i < bufs + handles; i++) {
		int dmaflags = 0;

		if (ctx->attrs && (ctx->attrs[i] & FASTRPC_ATTR_NOMAP))
			dmaflags = FASTRPC_DMAHANDLE_NOMAP;
		if (ctx->fds && (ctx->fds[i] != -1))
			err = fastrpc_mmap_create(ctx->fl, ctx->fds[i],
					FASTRPC_ATTR_NOVA, 0, 0, dmaflags,
					&ctx->maps[i]);
		if (!err && ctx->maps[i])
			ctx->maps[i]->ctx_refs++;
		if (err) {
			for (j = bufs; j < i; j++) {
				if (ctx->maps[j] && ctx->maps[j]->ctx_refs)
					ctx->maps[j]->ctx_refs--;
				fastrpc_mmap_free(ctx->maps[j], 0);
			}
			mutex_unlock(&ctx->fl->map_mutex);
			goto bail;
		}
		ipage += 1;
	}
	mutex_unlock(&ctx->fl->map_mutex);

	/* metalen includes meta data, fds, crc, dsp perf and early wakeup hint */
	metalen = totallen = (size_t)&ipage[0] + (sizeof(uint64_t) * M_FDLIST) +
			(sizeof(uint32_t) * M_CRCLIST) + (sizeof(uint64_t) * M_DSP_PERF_LIST) +
			sizeof(early_hint);

	if (metalen) {
		err = fastrpc_buf_alloc(ctx->fl, metalen, 0, 0,
				METADATA_BUF, &ctx->buf);
		if (err)
			goto bail;
		VERIFY(err, !IS_ERR_OR_NULL(ctx->buf->virt));
		if (err)
			goto bail;
		memset(ctx->buf->virt, 0, metalen);
	}
	ctx->used = metalen;

	/* allocate new local rpra buffer */
	lrpralen = (size_t)&list[0];
	if (lrpralen) {
		lrpra = kzalloc(lrpralen, GFP_KERNEL);
		VERIFY(err, !IS_ERR_OR_NULL(lrpra));
		if (err)
			goto bail;
	}
	ctx->lrpra = lrpra;

	/* calculate len required for copying */
	for (oix = 0; oix < inbufs + outbufs; ++oix) {
		int i = ctx->overps[oix]->raix;
		uintptr_t mstart, mend;
		size_t len = lpra[i].buf.len;

		if (!len)
			continue;
		if (ctx->maps[i])
			continue;
		if (ctx->overps[oix]->offset == 0)
			copylen = ALIGN(copylen, BALIGN);
		mstart = ctx->overps[oix]->mstart;
		mend = ctx->overps[oix]->mend;
		templen = mend - mstart;
		VERIFY(err, ((templen <= LONG_MAX) && (copylen <= (LONG_MAX - templen))));
		if (err) {
			err = -EFAULT;
			goto bail;
		}
		copylen += templen;
	}
	totallen = ALIGN(totallen, BALIGN) + copylen;

	/* allocate non -ion copy buffer */
	/* Checking if copylen can be accomodated in metalen*/
	/*if not allocating new buffer */
	if (totallen <= (size_t)buf_page_size(metalen)) {
		args = (uintptr_t)ctx->buf->virt + metalen;
		ctx->copybuf = ctx->buf;
		rlen = totallen - metalen;
	} else if (copylen) {
		err = fastrpc_buf_alloc(ctx->fl, copylen, 0, 0, COPYDATA_BUF,
				&ctx->copybuf);
		if (err)
			goto bail;
		memset(ctx->copybuf->virt, 0, copylen);
		args = (uintptr_t)ctx->copybuf->virt;
		rlen = copylen;
		totallen = copylen;
	}

	/* copy metadata */
	rpra = ctx->buf->virt;
	ctx->rpra = rpra;
	list = smq_invoke_buf_start(rpra, sc);
	pages = smq_phy_page_start(sc, list);
	ipage = pages;
	for (i = 0; i < bufs + handles; ++i) {
		if (lpra[i].buf.len)
			list[i].num = 1;
		else
			list[i].num = 0;
		list[i].pgidx = ipage - pages;
		ipage++;
	}

	/* map ion buffers */
	PERF(ctx->fl->profile, GET_COUNTER(perf_counter, PERF_MAP),
	for (i = 0; rpra && i < inbufs + outbufs; ++i) {
		struct fastrpc_mmap *map = ctx->maps[i];
		uint64_t buf = ptr_to_uint64(lpra[i].buf.pv);
		size_t len = lpra[i].buf.len;

		rpra[i].buf.pv = 0;
		rpra[i].buf.len = len;
		if (!len)
			continue;
		if (map) {
			struct vm_area_struct *vma;
			uintptr_t offset;
			uint64_t num = buf_num_pages(buf, len);
			int idx = list[i].pgidx;

			if (map->attr & FASTRPC_ATTR_NOVA) {
				offset = 0;
			} else {
				down_read(&current->mm->mmap_sem);
				VERIFY(err, NULL != (vma = find_vma(current->mm,
								map->va)));
				if (err) {
					up_read(&current->mm->mmap_sem);
					goto bail;
				}
				offset = buf_page_start(buf) - vma->vm_start;
				up_read(&current->mm->mmap_sem);
				VERIFY(err, offset + len <= (uintptr_t)map->size);
				if (err) {
					ADSPRPC_ERR(
						"buffer address is invalid for the fd passed for %d address 0x%llx and size %zu\n",
						i, (uintptr_t)lpra[i].buf.pv,
						lpra[i].buf.len);
					err = -EFAULT;
					goto bail;
				}
			}
			pages[idx].addr = map->phys + offset;
			pages[idx].size = num << PAGE_SHIFT;
		}
		rpra[i].buf.pv = buf;
	}
	PERF_END);
	for (i = bufs; i < bufs + handles; ++i) {
		struct fastrpc_mmap *map = ctx->maps[i];
		if (map) {
			pages[i].addr = map->phys;
			pages[i].size = map->size;
		}
	}
	fdlist = (uint64_t *)&pages[bufs + handles];
	crclist = (uint32_t *)&fdlist[M_FDLIST];
	/* reset fds, crc and early wakeup hint memory */
	/* remote process updates these values before responding */
	memset(fdlist, 0, sizeof(uint64_t)*M_FDLIST + sizeof(uint32_t)*M_CRCLIST +
			(sizeof(uint64_t) * M_DSP_PERF_LIST) + sizeof(early_hint));

	/* copy non ion buffers */
	PERF(ctx->fl->profile, GET_COUNTER(perf_counter, PERF_COPY),
	for (oix = 0; rpra && oix < inbufs + outbufs; ++oix) {
		int i = ctx->overps[oix]->raix;
		struct fastrpc_mmap *map = ctx->maps[i];
		size_t mlen;
		uint64_t buf;
		size_t len = lpra[i].buf.len;

		if (!len)
			continue;
		if (map)
			continue;
		if (ctx->overps[oix]->offset == 0) {
			rlen -= ALIGN(args, BALIGN) - args;
			args = ALIGN(args, BALIGN);
		}
		mlen = ctx->overps[oix]->mend - ctx->overps[oix]->mstart;
		VERIFY(err, rlen >= mlen);
		if (err)
			goto bail;
		rpra[i].buf.pv =
			 (args - ctx->overps[oix]->offset);
		pages[list[i].pgidx].addr = ctx->copybuf->phys -
					    ctx->overps[oix]->offset +
					    (totallen - rlen);
		pages[list[i].pgidx].addr =
			buf_page_start(pages[list[i].pgidx].addr);
		buf = rpra[i].buf.pv;
		pages[list[i].pgidx].size = buf_num_pages(buf, len) * PAGE_SIZE;
		if (i < inbufs) {
			K_COPY_FROM_USER(err, kernel, uint64_to_ptr(buf),
					lpra[i].buf.pv, len);
			if (err) {
				ADSPRPC_ERR(
					"copy from user failed with %d for dst 0x%llx, src %pK, size 0x%zx, arg %d\n",
					err, buf, lpra[i].buf.pv, len, i+1);
				err = -EFAULT;
				goto bail;
			}
		}
		if (len > DEBUG_PRINT_SIZE_LIMIT)
			ADSPRPC_DEBUG(
				"copied non ion buffer sc 0x%x pv 0x%llx, mend 0x%llx mstart 0x%llx, len %zu\n",
				sc, rpra[i].buf.pv,
				ctx->overps[oix]->mend,
				ctx->overps[oix]->mstart, len);
		args = args + mlen;
		rlen -= mlen;
	}
	PERF_END);

	PERF(ctx->fl->profile, GET_COUNTER(perf_counter, PERF_FLUSH),
	for (oix = 0; oix < inbufs + outbufs; ++oix) {
		int i = ctx->overps[oix]->raix;
		struct fastrpc_mmap *map = ctx->maps[i];

		if (i+1 > inbufs)	// Avoiding flush for outbufs
			continue;
		if (map && map->uncached)
			continue;
		if (ctx->fl->sctx && ctx->fl->sctx->smmu.coherent)
			continue;
		if (map && (map->attr & FASTRPC_ATTR_FORCE_NOFLUSH))
			continue;

		if (rpra && rpra[i].buf.len && (ctx->overps[oix]->mstart ||
		ctx->overps[oix]->do_cmo == 1)) {
			if (map && map->buf) {
				if (((buf_page_size(ctx->overps[oix]->mend -
				ctx->overps[oix]->mstart)) == map->size) ||
				ctx->overps[oix]->do_cmo) {
					dma_buf_begin_cpu_access(map->buf,
						DMA_TO_DEVICE);
					dma_buf_end_cpu_access(map->buf,
						DMA_TO_DEVICE);
					ADSPRPC_DEBUG(
						"sc 0x%x pv 0x%llx, mend 0x%llx mstart 0x%llx, len %zu size %zu\n",
						sc, rpra[i].buf.pv,
						ctx->overps[oix]->mend,
						ctx->overps[oix]->mstart,
						rpra[i].buf.len, map->size);
				} else {
					uintptr_t offset;
					uint64_t flush_len;
					struct vm_area_struct *vma;

					down_read(&current->mm->mmap_sem);
					VERIFY(err, NULL != (vma = find_vma(
						current->mm, rpra[i].buf.pv)));
					if (err) {
						up_read(&current->mm->mmap_sem);
						goto bail;
					}
					if (ctx->overps[oix]->do_cmo) {
						offset = rpra[i].buf.pv -
								vma->vm_start;
						flush_len = rpra[i].buf.len;
					} else {
						offset =
						ctx->overps[oix]->mstart
						- vma->vm_start;
						flush_len =
						ctx->overps[oix]->mend -
						ctx->overps[oix]->mstart;
					}
					up_read(&current->mm->mmap_sem);
					dma_buf_begin_cpu_access_partial(
						map->buf, DMA_TO_DEVICE, offset,
						flush_len);
					dma_buf_end_cpu_access_partial(
						map->buf, DMA_TO_DEVICE, offset,
						flush_len);
					ADSPRPC_DEBUG(
						"sc 0x%x vm_start 0x%llx pv 0x%llx, offset 0x%llx, mend 0x%llx mstart 0x%llx, len %zu size %zu\n",
						sc, vma->vm_start,
						rpra[i].buf.pv, offset,
						ctx->overps[oix]->mend,
						ctx->overps[oix]->mstart,
						rpra[i].buf.len, map->size);
				}
			}
		}
	}
	PERF_END);

	for (i = bufs; ctx->fds && rpra && i < bufs + handles; i++) {
		rpra[i].dma.fd = ctx->fds[i];
		rpra[i].dma.len = (uint32_t)lpra[i].buf.len;
		rpra[i].dma.offset =
				(uint32_t)(uintptr_t)lpra[i].buf.pv;
	}

	/* Copy rpra to local buffer */
	if (ctx->lrpra && rpra && lrpralen > 0)
		memcpy(ctx->lrpra, rpra, lrpralen);
 bail:
	return err;
}

static int put_args(uint32_t kernel, struct smq_invoke_ctx *ctx,
		    remote_arg_t *upra)
{
	uint32_t sc = ctx->sc;
	struct smq_invoke_buf *list;
	struct smq_phy_page *pages;
	struct fastrpc_mmap *mmap;
	uint64_t *fdlist;
	uint32_t *crclist = NULL, *poll = NULL;
	uint64_t *perf_dsp_list = NULL;

	remote_arg64_t *rpra = ctx->lrpra;
	int i, inbufs, outbufs, handles;
	int err = 0;

	inbufs = REMOTE_SCALARS_INBUFS(sc);
	outbufs = REMOTE_SCALARS_OUTBUFS(sc);
	handles = REMOTE_SCALARS_INHANDLES(sc) + REMOTE_SCALARS_OUTHANDLES(sc);
	list = smq_invoke_buf_start(ctx->rpra, sc);
	pages = smq_phy_page_start(sc, list);
	fdlist = (uint64_t *)(pages + inbufs + outbufs + handles);
	crclist = (uint32_t *)(fdlist + M_FDLIST);
	poll = (uint32_t *)(crclist + M_CRCLIST);
	perf_dsp_list = (uint64_t *)(poll + 1);

	for (i = inbufs; i < inbufs + outbufs; ++i) {
		if (!ctx->maps[i]) {
			K_COPY_TO_USER(err, kernel,
				ctx->lpra[i].buf.pv,
				uint64_to_ptr(rpra[i].buf.pv),
				rpra[i].buf.len);
			if (err) {
				ADSPRPC_ERR(
					"Invalid size 0x%llx for output argument %d ret %ld\n",
					rpra[i].buf.len, i+1, err);
				err = -EFAULT;
				goto bail;
			}
		} else {
			mutex_lock(&ctx->fl->map_mutex);
			if (ctx->maps[i]->ctx_refs)
				ctx->maps[i]->ctx_refs--;
			fastrpc_mmap_free(ctx->maps[i], 0);
			mutex_unlock(&ctx->fl->map_mutex);
			ctx->maps[i] = NULL;
		}
	}
	mutex_lock(&ctx->fl->map_mutex);
	for (i = 0; i < M_FDLIST; i++) {
		if (!fdlist[i])
			break;
		if (!fastrpc_mmap_find(ctx->fl, (int)fdlist[i], 0, 0,
					0, 0, &mmap)) {
			if (mmap && mmap->ctx_refs)
				mmap->ctx_refs--;
			fastrpc_mmap_free(mmap, 0);
		}
	}
	mutex_unlock(&ctx->fl->map_mutex);
	if (ctx->crc && crclist && rpra)
		K_COPY_TO_USER(err, kernel, ctx->crc,
			crclist, M_CRCLIST*sizeof(uint32_t));
	if (ctx->perf_dsp && perf_dsp_list)
		K_COPY_TO_USER_WITHOUT_ERR(kernel, ctx->perf_dsp,
			perf_dsp_list, M_DSP_PERF_LIST*sizeof(uint64_t));

 bail:
	return err;
}

static void inv_args(struct smq_invoke_ctx *ctx)
{
	int i, inbufs, outbufs;
	uint32_t sc = ctx->sc;
	remote_arg64_t *rpra = ctx->lrpra;
	int err = 0;

	inbufs = REMOTE_SCALARS_INBUFS(sc);
	outbufs = REMOTE_SCALARS_OUTBUFS(sc);
	for (i = 0; i < inbufs + outbufs; ++i) {
		int over = ctx->overps[i]->raix;
		struct fastrpc_mmap *map = ctx->maps[over];

		if ((over + 1 <= inbufs))
			continue;

		if (map && map->uncached)
			continue;
		if (!rpra[over].buf.len)
			continue;
		if (ctx->fl && ctx->fl->sctx && ctx->fl->sctx->smmu.coherent)
			continue;
		if (map && (map->attr & FASTRPC_ATTR_FORCE_NOINVALIDATE))
			continue;

		if (buf_page_start(ptr_to_uint64((void *)rpra)) ==
				buf_page_start(rpra[over].buf.pv)) {
			continue;
		}
		if (ctx->overps[i]->mstart || ctx->overps[i]->do_cmo == 1) {
			if (map && map->buf) {
				if (((buf_page_size(ctx->overps[i]->mend -
				ctx->overps[i]->mstart)) == map->size) ||
				ctx->overps[i]->do_cmo) {
					dma_buf_begin_cpu_access(map->buf,
						DMA_TO_DEVICE);
					dma_buf_end_cpu_access(map->buf,
						DMA_FROM_DEVICE);
					ADSPRPC_DEBUG(
						"sc 0x%x pv 0x%llx, mend 0x%llx mstart 0x%llx, len %zu size %zu\n",
						sc, rpra[over].buf.pv,
						ctx->overps[i]->mend,
						ctx->overps[i]->mstart,
						rpra[over].buf.len, map->size);
				} else {
					uintptr_t offset;
					uint64_t inv_len;
					struct vm_area_struct *vma;

					down_read(&current->mm->mmap_sem);
					VERIFY(err, NULL != (vma = find_vma(
						current->mm,
						rpra[over].buf.pv)));
					if (err) {
						up_read(&current->mm->mmap_sem);
						goto bail;
					}
					if (ctx->overps[i]->do_cmo) {
						offset = rpra[over].buf.pv -
								vma->vm_start;
						inv_len = rpra[over].buf.len;
					} else {
						offset =
							ctx->overps[i]->mstart -
							vma->vm_start;
						inv_len =
							ctx->overps[i]->mend -
							ctx->overps[i]->mstart;
					}
					up_read(&current->mm->mmap_sem);
					dma_buf_begin_cpu_access_partial(
						map->buf, DMA_TO_DEVICE, offset,
						inv_len);
					dma_buf_end_cpu_access_partial(map->buf,
						DMA_FROM_DEVICE, offset,
						inv_len);
					ADSPRPC_DEBUG(
						"sc 0x%x vm_start 0x%llx pv 0x%llx, offset 0x%llx, mend 0x%llx mstart 0x%llx, len %zu size %zu\n",
						sc, vma->vm_start,
						rpra[over].buf.pv,
						offset, ctx->overps[i]->mend,
						ctx->overps[i]->mstart,
						rpra[over].buf.len, map->size);
				}
			}
		}
	}
bail:
	return;
}

static int fastrpc_invoke_send(struct smq_invoke_ctx *ctx,
			       uint32_t kernel, uint32_t handle)
{
	struct smq_msg *msg = &ctx->msg;
	struct smq_msg msg_temp;
	struct fastrpc_file *fl = ctx->fl;
	struct fastrpc_channel_ctx *channel_ctx = NULL;
	int err = 0, cid = -1;
	uint32_t sc = ctx->sc;
	int64_t ns = 0;
	int isasync = (ctx->asyncjob.isasyncjob ? true : false);

	if (!fl) {
		err = -EBADF;
		goto bail;
	}
	cid = fl->cid;
	VERIFY(err, cid >= ADSP_DOMAIN_ID && cid < NUM_CHANNELS);
	if (err) {
		err = -ECHRNG;
		goto bail;
	}

	channel_ctx = &fl->apps->channel[cid];
	mutex_lock(&channel_ctx->smd_mutex);
	msg->pid = fl->tgid;
	msg->tid = current->pid;
	if (fl->sessionid)
		msg->tid |= (1 << SESSION_ID_INDEX);
	if (kernel == KERNEL_MSG_WITH_ZERO_PID)
		msg->pid = 0;
	msg->invoke.header.ctx = ctx->ctxid | fl->pd;
	msg->invoke.header.handle = handle;
	msg->invoke.header.sc = sc;
	msg->invoke.page.addr = ctx->buf ? ctx->buf->phys : 0;
	msg->invoke.page.size = buf_page_size(ctx->used);

	if (fl->ssrcount != channel_ctx->ssrcount) {
		err = -ECONNRESET;
		mutex_unlock(&channel_ctx->smd_mutex);
		goto bail;
	}
	mutex_unlock(&channel_ctx->smd_mutex);

	mutex_lock(&channel_ctx->rpmsg_mutex);
	VERIFY(err, !IS_ERR_OR_NULL(channel_ctx->rpdev));
	if (err) {
		err = -ENODEV;
		mutex_unlock(&channel_ctx->rpmsg_mutex);
		goto bail;
	}

	if (isasync) {
		/*
		 * After message is sent to DSP, async response thread could immediately
		 * get the response and free context, which will result in a use-after-free
		 * in this function. So use a local variable for message.
		 */
		memcpy(&msg_temp, msg, sizeof(struct smq_msg));
		msg = &msg_temp;
	}

	err = rpmsg_send(channel_ctx->rpdev->ept, (void *)msg, sizeof(*msg));
	mutex_unlock(&channel_ctx->rpmsg_mutex);
	trace_fastrpc_rpmsg_send(cid, (uint64_t)ctx, msg->invoke.header.ctx,
		handle, sc, msg->invoke.page.addr, msg->invoke.page.size);
	ns = get_timestamp_in_ns();
	fastrpc_update_txmsg_buf(channel_ctx, msg, err, ns);
 bail:
	return err;
}

/*
 * name : fastrpc_get_dsp_status
 * @in  : pointer to fastrpc_apps
 * @out : void
 * Description : This function reads the property
 * string from device node and updates the cdsp device
 * avialbility status if the node belongs to cdsp device.
 */

static void fastrpc_get_dsp_status(struct fastrpc_apps *me)
{
	int ret = -1;
	struct device_node *node = NULL;
	const char *name = NULL;

	do {
		node = of_find_compatible_node(node, NULL, "qcom,pil-tz-generic");
		if (node) {
			ret = of_property_read_string(node, "qcom,firmware-name", &name);
			if (!strcmp(name, "cdsp")) {
				ret =  of_device_is_available(node);
				me->remote_cdsp_status = ret;
				ADSPRPC_INFO("adsprpc: %s: cdsp node found with ret:%x\n",
						__func__, ret);
				break;
			}
		} else {
			ADSPRPC_ERR("adsprpc: Error: %s: cdsp node not found\n", __func__);
			break;
		}
	} while (1);
}

static void fastrpc_init(struct fastrpc_apps *me)
{
	int i;

	INIT_HLIST_HEAD(&me->drivers);
	INIT_HLIST_HEAD(&me->maps);
	spin_lock_init(&me->hlock);
	me->channel = &gcinfo[0];
	mutex_init(&me->mut_uid);
	for (i = 0; i < NUM_CHANNELS; i++) {
		init_completion(&me->channel[i].work);
		init_completion(&me->channel[i].workport);
		me->channel[i].sesscount = 0;
		/* All channels are secure by default except CDSP */
		me->channel[i].secure = SECURE_CHANNEL;
		me->channel[i].unsigned_support = false;
		mutex_init(&me->channel[i].smd_mutex);
		mutex_init(&me->channel[i].rpmsg_mutex);
		spin_lock_init(&me->channel[i].ctxlock);
		spin_lock_init(&me->channel[i].gmsg_log.lock);
		INIT_HLIST_HEAD(&me->channel[i].initmems);
	}
	/* Set CDSP channel to non secure */
	me->channel[CDSP_DOMAIN_ID].secure = NON_SECURE_CHANNEL;
	me->channel[CDSP_DOMAIN_ID].unsigned_support = true;
}

static inline void fastrpc_pm_awake(struct fastrpc_file *fl, int channel_type)
{
	struct fastrpc_apps *me = &gfa;
	struct wakeup_source *wake_source = NULL;

	if (!fl->wake_enable)
		return;
	/*
	 * Vote with PM to abort any suspend in progress and
	 * keep system awake for specified timeout
	 */
	if (channel_type == SECURE_CHANNEL)
		wake_source = me->wake_source_secure;
	else if (channel_type == NON_SECURE_CHANNEL)
		wake_source = me->wake_source;

	if (wake_source)
		pm_wakeup_ws_event(wake_source, fl->ws_timeout, true);
}

static inline int fastrpc_wait_for_response(struct smq_invoke_ctx *ctx,
						uint32_t kernel)
{
	int interrupted = 0;

	if (kernel)
		wait_for_completion(&ctx->work);
	else
		interrupted = wait_for_completion_interruptible(&ctx->work);

	return interrupted;
}

static void fastrpc_wait_for_completion(struct smq_invoke_ctx *ctx,
			int *ptr_interrupted, uint32_t kernel, uint32_t async,
			bool *ptr_isworkdone)
{
	int interrupted = 0, err = 0;
	int jj;
	bool wait_resp;
	uint32_t wTimeout = FASTRPC_USER_EARLY_HINT_TIMEOUT;
	uint32_t wakeTime = 0;
	unsigned long flags;

	if (!ctx) {
		/* This failure is not expected */
		err = *ptr_interrupted = EFAULT;
		*ptr_isworkdone = false;
		ADSPRPC_ERR("ctx is NULL, cannot wait for response err %d\n",
					err);
		return;
	}
	wakeTime = ctx->early_wake_time;

	do {
		switch (ctx->rsp_flags) {
		/* try polling on completion with timeout */
		case USER_EARLY_SIGNAL:
			/* try wait if completion time is less than timeout */
			/* disable preempt to avoid context switch latency */
			preempt_disable();
			jj = 0;
			wait_resp = false;
			for (; wakeTime < wTimeout && jj < wTimeout; jj++) {
				wait_resp = try_wait_for_completion(&ctx->work);
				if (wait_resp)
					break;
				udelay(1);
			}
			preempt_enable();
			if (async) {
				spin_lock_irqsave(&ctx->fl->aqlock, flags);
				if (!ctx->is_work_done) {
					ctx->is_early_wakeup = false;
					*ptr_isworkdone = false;
				} else
					*ptr_isworkdone = true;
				spin_unlock_irqrestore(&ctx->fl->aqlock, flags);
				goto bail;
			} else if (!wait_resp) {
				interrupted = fastrpc_wait_for_response(ctx,
									kernel);
				*ptr_interrupted = interrupted;
				if (interrupted || ctx->is_work_done)
					goto bail;
			}
			break;

		/* busy poll on memory for actual job done */
		case EARLY_RESPONSE:
			err = poll_on_early_response(ctx);

			/* Mark job done if poll on memory successful */
			/* Wait for completion if poll on memory timoeut */
			if (!err) {
				ctx->is_work_done = true;
				*ptr_isworkdone = true;
				goto bail;
			}
			ADSPRPC_INFO("poll timeout for handle 0x%x, sc 0x%x\n",
				ctx->handle, ctx->sc);
			if (async) {
				spin_lock_irqsave(&ctx->fl->aqlock, flags);
				if (!ctx->is_work_done) {
					ctx->is_early_wakeup = false;
					*ptr_isworkdone = false;
				} else
					*ptr_isworkdone = true;
				spin_unlock_irqrestore(&ctx->fl->aqlock, flags);
				goto bail;
			} else if (!ctx->is_work_done) {
				interrupted = fastrpc_wait_for_response(ctx,
									kernel);
				*ptr_interrupted = interrupted;
				if (interrupted || ctx->is_work_done)
					goto bail;
			}
			break;

		case COMPLETE_SIGNAL:
		case NORMAL_RESPONSE:
			if (!async) {
				interrupted = fastrpc_wait_for_response(ctx,
								kernel);
				*ptr_interrupted = interrupted;
				if (interrupted || ctx->is_work_done)
					goto bail;
			} else {
				spin_lock_irqsave(&ctx->fl->aqlock, flags);
				if (!ctx->is_work_done) {
					ctx->is_early_wakeup = false;
					*ptr_isworkdone = false;
				} else
					*ptr_isworkdone = true;
				spin_unlock_irqrestore(&ctx->fl->aqlock, flags);
				goto bail;
			}
			break;
		default:
			*ptr_interrupted = EBADR;
			*ptr_isworkdone = false;
			ADSPRPC_ERR(
				"unsupported response flags 0x%x for handle 0x%x, sc 0x%x\n",
				ctx->rsp_flags, ctx->handle, ctx->sc);
			goto bail;
		} /* end of switch */
	} while (!ctx->is_work_done);
bail:
	return;
}

static void fastrpc_update_invoke_count(uint32_t handle, uint64_t *perf_counter,
					struct timespec64 *invoket)
{
	/* update invoke count for dynamic handles */
	if (handle != FASTRPC_STATIC_HANDLE_LISTENER) {
		uint64_t *count = GET_COUNTER(perf_counter, PERF_INVOKE);

		if (count)
			*count += getnstimediff(invoket);
	}
	if (handle > FASTRPC_STATIC_HANDLE_MAX) {
		uint64_t *count = GET_COUNTER(perf_counter, PERF_COUNT);

		if (count)
			*count += 1;
	}
}

static int fastrpc_internal_invoke(struct fastrpc_file *fl, uint32_t mode,
				   uint32_t kernel,
				   struct fastrpc_ioctl_invoke_async *inv)
{
	struct smq_invoke_ctx *ctx = NULL;
	struct fastrpc_ioctl_invoke *invoke = &inv->inv;
	int err = 0, interrupted = 0, cid = -1;
	struct timespec64 invoket = {0};
	uint64_t *perf_counter = NULL;
	bool isasyncinvoke = false, isworkdone = false;

	cid = fl->cid;
	VERIFY(err, cid >= ADSP_DOMAIN_ID && cid < NUM_CHANNELS &&
			fl->sctx != NULL);
	if (err) {
		ADSPRPC_ERR("kernel session not initialized yet for %s\n",
			current->comm);
		err = -EBADR;
		goto bail;
	}

	if (fl->profile) {
		ktime_get_real_ts64(&invoket);
	}

	if (!kernel) {
		VERIFY(err, invoke->handle !=
			FASTRPC_STATIC_HANDLE_PROCESS_GROUP);
		VERIFY(err, invoke->handle !=
			FASTRPC_STATIC_HANDLE_DSP_UTILITIES);
		if (err) {
			err = -EINVAL;
			ADSPRPC_ERR(
				"user application %s trying to send a kernel RPC message to channel %d, handle 0x%x\n",
				cid, invoke->handle);
			goto bail;
		}
	}

	if (!kernel) {
		VERIFY(err, 0 == (err = context_restore_interrupted(fl,
		inv, &ctx)));
		if (err)
			goto bail;
		if (fl->sctx->smmu.faults)
			err = -FASTRPC_ENOSUCH;
		if (err)
			goto bail;
		if (ctx) {
			trace_fastrpc_context_restore(cid, (uint64_t)ctx,
				ctx->msg.invoke.header.ctx,
				ctx->handle, ctx->sc);
			goto wait;
		}
	}

	VERIFY(err, 0 == (err = context_alloc(fl, kernel, inv, &ctx)));
	if (err)
		goto bail;
	isasyncinvoke = (ctx->asyncjob.isasyncjob ? true : false);
	if (fl->profile)
		perf_counter = (uint64_t *)ctx->perf + PERF_COUNT;
	PERF(fl->profile, GET_COUNTER(perf_counter, PERF_GETARGS),
	VERIFY(err, 0 == (err = get_args(kernel, ctx)));
	PERF_END);
	if (err)
		goto bail;

	PERF(fl->profile, GET_COUNTER(perf_counter, PERF_INVARGS),
	inv_args(ctx);
	PERF_END);

	PERF(fl->profile, GET_COUNTER(perf_counter, PERF_LINK),
	VERIFY(err, 0 == (err = fastrpc_invoke_send(ctx,
		kernel, invoke->handle)));
	PERF_END);

	if (err)
		goto bail;
	if (isasyncinvoke)
		goto invoke_end;
 wait:
	fastrpc_wait_for_completion(ctx, &interrupted, kernel, 0, &isworkdone);
	VERIFY(err, 0 == (err = interrupted));
	if (err)
		goto bail;

	if (!ctx->is_work_done) {
		err = -ETIMEDOUT;
		ADSPRPC_ERR(
			"WorkDone state is invalid for handle 0x%x, sc 0x%x\n",
			invoke->handle, ctx->sc);
		goto bail;
	}

	PERF(fl->profile, GET_COUNTER(perf_counter, PERF_INVARGS),
	inv_args(ctx);
	PERF_END);

	PERF(fl->profile, GET_COUNTER(perf_counter, PERF_PUTARGS),
	VERIFY(err, 0 == (err = put_args(kernel, ctx, invoke->pra)));
	PERF_END);
	if (err)
		goto bail;

	VERIFY(err, 0 == (err = ctx->retval));
	if (err)
		goto bail;
 bail:
	if (ctx && interrupted == -ERESTARTSYS) {
		trace_fastrpc_context_interrupt(cid, (uint64_t)ctx,
			ctx->msg.invoke.header.ctx, ctx->handle, ctx->sc);
		context_save_interrupted(ctx);
	} else if (ctx) {
		if (fl->profile && !interrupted)
			fastrpc_update_invoke_count(invoke->handle,
				perf_counter, &invoket);
		if (fl->profile && ctx->perf && ctx->handle > FASTRPC_STATIC_HANDLE_MAX) {
			trace_fastrpc_perf_counters(ctx->handle, ctx->sc,
			ctx->perf->count, ctx->perf->flush, ctx->perf->map,
			ctx->perf->copy, ctx->perf->link, ctx->perf->getargs,
			ctx->perf->putargs, ctx->perf->invargs,
			ctx->perf->invoke, ctx->perf->tid);
			if (ctx->perf_kernel)
				K_COPY_TO_USER_WITHOUT_ERR(kernel, ctx->perf_kernel,
				ctx->perf, M_KERNEL_PERF_LIST*sizeof(uint64_t));
		}
		context_free(ctx);
	}
	if (fl->ssrcount != fl->apps->channel[cid].ssrcount)
		err = -ECONNRESET;

invoke_end:
	if (fl->profile && !interrupted && isasyncinvoke)
		fastrpc_update_invoke_count(invoke->handle, perf_counter,
						&invoket);
	return err;
}

static int fastrpc_wait_on_async_queue(
			struct fastrpc_ioctl_async_response *async_res,
			struct fastrpc_file *fl)
{
	int err = 0, ierr = 0, interrupted = 0;
	struct smq_invoke_ctx *ctx = NULL, *ictx = NULL, *n = NULL;
	unsigned long flags;
	uint64_t *perf_counter = NULL;
	bool isworkdone = false;

read_async_job:
	interrupted = wait_event_interruptible(fl->async_wait_queue,
				atomic_read(&fl->async_queue_job_count));
	if (!fl || fl->file_close >= FASTRPC_PROCESS_EXIT_START) {
		err = -EBADF;
		goto bail;
	}
	VERIFY(err, 0 == (err = interrupted));
	if (err)
		goto bail;

	spin_lock_irqsave(&fl->aqlock, flags);
	list_for_each_entry_safe(ictx, n, &fl->clst.async_queue, asyncn) {
		list_del_init(&ictx->asyncn);
		atomic_sub(1, &fl->async_queue_job_count);
		ctx = ictx;
		break;
	}
	spin_unlock_irqrestore(&fl->aqlock, flags);
	if (fl->profile && ctx)
		perf_counter = (uint64_t *)ctx->perf + PERF_COUNT;
	if (ctx) {
		fastrpc_wait_for_completion(ctx, &interrupted, 0, 1,
							&isworkdone);
		if (!isworkdone) {//In valid workdone state
			ADSPRPC_DEBUG(
				"Async early wake response did not reach on time for thread %d handle 0x%x, sc 0x%x\n",
				ctx->pid, ctx->handle, ctx->sc);
			goto read_async_job;
		}
		async_res->jobid = ctx->asyncjob.jobid;
		async_res->result = ctx->retval;
		async_res->handle = ctx->handle;
		async_res->sc = ctx->sc;
		async_res->perf_dsp = (uint64_t *)ctx->perf_dsp;
		async_res->perf_kernel = (uint64_t *)ctx->perf_kernel;

		PERF(ctx->fl->profile, GET_COUNTER(perf_counter, PERF_INVARGS),
		inv_args(ctx);
		PERF_END);
		if (ctx->retval != 0)
			goto bail;
		PERF(ctx->fl->profile, GET_COUNTER(perf_counter, PERF_PUTARGS),
		VERIFY(ierr, 0 == (ierr = put_args(0, ctx, NULL)));
		PERF_END);
		if (ierr)
			goto bail;
	} else {//Retry again if ctx is NULL
		ADSPRPC_ERR("Invalid async job wake up\n");
		goto read_async_job;
	}
bail:
	if (ierr)
		async_res->result = ierr;
	if (ctx) {
		if (fl->profile && ctx->perf && ctx->handle > FASTRPC_STATIC_HANDLE_MAX) {
			trace_fastrpc_perf_counters(ctx->handle, ctx->sc,
			ctx->perf->count, ctx->perf->flush, ctx->perf->map,
			ctx->perf->copy, ctx->perf->link, ctx->perf->getargs,
			ctx->perf->putargs, ctx->perf->invargs,
			ctx->perf->invoke, ctx->perf->tid);
			if (ctx->perf_kernel)
				K_COPY_TO_USER_WITHOUT_ERR(0, ctx->perf_kernel,
				ctx->perf, M_KERNEL_PERF_LIST*sizeof(uint64_t));
		}
		context_free(ctx);
	}
	return err;
}

static int fastrpc_get_async_response(
		struct fastrpc_ioctl_async_response *async_res,
			void *param, struct fastrpc_file *fl)
{
	int err = 0;

	err = fastrpc_wait_on_async_queue(async_res, fl);
	if (err)
		goto bail;
	K_COPY_TO_USER(err, 0, param, async_res,
			sizeof(struct fastrpc_ioctl_async_response));
bail:
	return err;
}

static int fastrpc_create_persistent_headers(struct fastrpc_file *fl,
			uint32_t user_concurrency)
{
	int err = 0, i = 0;
	uint64_t virtb = 0;
	struct fastrpc_buf *pers_hdr_buf = NULL, *hdr_bufs = NULL, *buf = NULL;
	unsigned int num_pers_hdrs = 0;
	size_t hdr_buf_alloc_len = 0;

	if (fl->pers_hdr_buf || !user_concurrency)
		goto bail;

	/*
	 * Pre-allocate memory for persistent header buffers based
	 * on concurrency info passed by user. Upper limit enforced.
	 */
	num_pers_hdrs = (user_concurrency > MAX_PERSISTENT_HEADERS) ?
		MAX_PERSISTENT_HEADERS : user_concurrency;
	hdr_buf_alloc_len = num_pers_hdrs*PAGE_SIZE;
	err = fastrpc_buf_alloc(fl, hdr_buf_alloc_len, 0, 0,
			METADATA_BUF, &pers_hdr_buf);
	if (err)
		goto bail;
	virtb = ptr_to_uint64(pers_hdr_buf->virt);

	/* Map entire buffer on remote subsystem in single RPC call */
	err = fastrpc_mem_map_to_dsp(fl, -1, 0, ADSP_MMAP_PERSIST_HDR, 0,
			pers_hdr_buf->phys, pers_hdr_buf->size,
			&pers_hdr_buf->raddr);
	if (err)
		goto bail;

	/* Divide and store as N chunks, each of 1 page size */
	hdr_bufs = kcalloc(num_pers_hdrs, sizeof(struct fastrpc_buf),
				GFP_KERNEL);
	if (!hdr_bufs) {
		err = -ENOMEM;
		goto bail;
	}
	spin_lock(&fl->hlock);
	fl->pers_hdr_buf = pers_hdr_buf;
	fl->num_pers_hdrs = num_pers_hdrs;
	fl->hdr_bufs = hdr_bufs;
	for (i = 0; i < num_pers_hdrs; i++) {
		buf = &fl->hdr_bufs[i];
		buf->fl = fl;
		buf->virt = uint64_to_ptr(virtb + (i*PAGE_SIZE));
		buf->phys = pers_hdr_buf->phys + (i*PAGE_SIZE);
		buf->size = PAGE_SIZE;
		buf->dma_attr = pers_hdr_buf->dma_attr;
		buf->flags = pers_hdr_buf->flags;
		buf->type = pers_hdr_buf->type;
		buf->in_use = false;
	}
	spin_unlock(&fl->hlock);
bail:
	if (err) {
		fl->pers_hdr_buf = NULL;
		fl->hdr_bufs = NULL;
		fl->num_pers_hdrs = 0;
		if (!IS_ERR_OR_NULL(pers_hdr_buf))
			fastrpc_buf_free(pers_hdr_buf, 0);
		if (!IS_ERR_OR_NULL(hdr_bufs))
			kfree(hdr_bufs);
	}
	return err;
}

static int fastrpc_internal_invoke2(struct fastrpc_file *fl,
				struct fastrpc_ioctl_invoke2 *inv2)
{
	union {
		struct fastrpc_ioctl_invoke_async inv;
		struct fastrpc_ioctl_invoke_async_no_perf inv3;
		struct fastrpc_ioctl_async_response async_res;
		uint32_t user_concurrency;
	} p;
	struct fastrpc_dsp_capabilities *dsp_cap_ptr = NULL;
	uint32_t size = 0;
	int err = 0, domain = fl->cid;

	if (inv2->req == FASTRPC_INVOKE2_ASYNC ||
		inv2->req == FASTRPC_INVOKE2_ASYNC_RESPONSE) {
		VERIFY(err, domain == CDSP_DOMAIN_ID && fl->sctx != NULL);
		if (err)
			goto bail;
		dsp_cap_ptr = &gcinfo[domain].dsp_cap_kernel;
		VERIFY(err,
			dsp_cap_ptr->dsp_attributes[ASYNC_FASTRPC_CAP] == 1);
		if (err) {
			err = -EPROTONOSUPPORT;
			goto bail;
		}
	}
	switch (inv2->req) {
	case FASTRPC_INVOKE2_ASYNC:
		size = sizeof(struct fastrpc_ioctl_invoke_async);
		VERIFY(err, size >= inv2->size);
		if (err) {
			err = -EBADE;
			goto bail;
		}
		if (size > inv2->size) {
			K_COPY_FROM_USER(err, 0, &p.inv3, (void *)inv2->invparam,
				sizeof(struct fastrpc_ioctl_invoke_async_no_perf));
			if (err)
				goto bail;
			memcpy(&p.inv, &p.inv3, sizeof(struct fastrpc_ioctl_invoke_crc));
			memcpy(&p.inv.job, &p.inv3.job, sizeof(p.inv.job));
		} else {
			K_COPY_FROM_USER(err, 0, &p.inv, (void *)inv2->invparam, size);
			if (err)
				goto bail;
		}
		VERIFY(err, 0 == (err = fastrpc_internal_invoke(fl, fl->mode,
					USER_MSG, &p.inv)));
		if (err)
			goto bail;
		break;
	case FASTRPC_INVOKE2_ASYNC_RESPONSE:
		VERIFY(err,
		sizeof(struct fastrpc_ioctl_async_response) >= inv2->size);
		if (err) {
			err = -EBADE;
			goto bail;
		}
		err = fastrpc_get_async_response(&p.async_res,
						(void *)inv2->invparam, fl);
		break;
	case FASTRPC_INVOKE2_KERNEL_OPTIMIZATIONS:
		size = sizeof(uint32_t);
		if (inv2->size != size) {
			err = -EBADE;
			goto bail;
		}
		K_COPY_FROM_USER(err, 0, &p.user_concurrency,
				(void *)inv2->invparam, size);
		if (err)
			goto bail;
		err = fastrpc_create_persistent_headers(fl,
				p.user_concurrency);
		break;
	default:
		err = -ENOTTY;
		break;
	}
bail:
	return err;
}

static int fastrpc_get_spd_session(char *name, int *session, int *cid)
{
	struct fastrpc_apps *me = &gfa;
	int err = 0, i, j, match = 0;

	for (i = 0; i < NUM_CHANNELS; i++) {
		for (j = 0; j < NUM_SESSIONS; j++) {
			if (!me->channel[i].spd[j].servloc_name)
				continue;
			if (!strcmp(name, me->channel[i].spd[j].servloc_name)) {
				match = 1;
				break;
			}
		}
		if (match)
			break;
	}
	VERIFY(err, i < NUM_CHANNELS && j < NUM_SESSIONS);
	if (err) {
		err = -EUSERS;
		goto bail;
	}
	*cid = i;
	*session = j;
bail:
	return err;
}

static int fastrpc_mmap_remove_pdr(struct fastrpc_file *fl);
static int fastrpc_channel_open(struct fastrpc_file *fl);
static int fastrpc_mmap_remove_ssr(struct fastrpc_file *fl);

/*
 * This function makes a call to create a thread group in the root
 * process or static process on the remote subsystem.
 * Examples:
 *		- guestOS daemons on all DSPs
 *		- sensors daemon on sensorsPD on SLPI/ADSP
 */
static int fastrpc_init_attach_process(struct fastrpc_file *fl,
					struct fastrpc_ioctl_init *init)
{
	int err = 0, tgid = fl->tgid;
	remote_arg_t ra[1];
	struct fastrpc_ioctl_invoke_async ioctl;

	if (fl->dev_minor == MINOR_NUM_DEV) {
		err = -ECONNREFUSED;
		ADSPRPC_ERR(
			"untrusted app trying to attach to privileged DSP PD\n");
		return err;
	}
	/*
	 * Prepare remote arguments for creating thread group
	 * in guestOS/staticPD on the remote subsystem.
	 */
	ra[0].buf.pv = (void *)&tgid;
	ra[0].buf.len = sizeof(tgid);
	ioctl.inv.handle = FASTRPC_STATIC_HANDLE_PROCESS_GROUP;
	ioctl.inv.sc = REMOTE_SCALARS_MAKE(0, 1, 0);
	ioctl.inv.pra = ra;
	ioctl.fds = NULL;
	ioctl.attrs = NULL;
	ioctl.crc = NULL;
	ioctl.perf_kernel = NULL;
	ioctl.perf_dsp = NULL;
	ioctl.job = NULL;

	if (init->flags == FASTRPC_INIT_ATTACH)
		fl->pd = 0;
	else if (init->flags == FASTRPC_INIT_ATTACH_SENSORS) {
		if (fl->cid == ADSP_DOMAIN_ID)
			fl->servloc_name =
			SENSORS_PDR_ADSP_SERVICE_LOCATION_CLIENT_NAME;
		else if (fl->cid == SDSP_DOMAIN_ID)
			fl->servloc_name =
			SENSORS_PDR_SLPI_SERVICE_LOCATION_CLIENT_NAME;
		/* Setting to 2 will route the message to sensorsPD */
		fl->pd = 2;
	}

	err = fastrpc_internal_invoke(fl, FASTRPC_MODE_PARALLEL, KERNEL_MSG_WITH_ZERO_PID, &ioctl);
	if (err)
		goto bail;
bail:
	return err;
}

/*
 * This function makes a call to spawn a dynamic process
 * on the remote subsystem.
 * Example: all compute offloads to CDSP
 */
static int fastrpc_init_create_dynamic_process(struct fastrpc_file *fl,
				struct fastrpc_ioctl_init_attrs *uproc)
{
	int err = 0, memlen = 0, mflags = 0, locked = 0;
	struct fastrpc_ioctl_invoke_async ioctl;
	struct fastrpc_ioctl_init *init = &uproc->init;
	struct smq_phy_page pages[1];
	struct fastrpc_mmap *file = NULL;
	struct fastrpc_buf *imem = NULL;
	unsigned long imem_dma_attr = 0;
	remote_arg_t ra[6];
	int fds[6];
	unsigned int gid = 0, one_mb = 1024*1024;
	unsigned int dsp_userpd_memlen = 3 * one_mb;
	struct fastrpc_buf *init_mem;

	struct {
		int pgid;
		unsigned int namelen;
		unsigned int filelen;
		unsigned int pageslen;
		int attrs;
		int siglen;
	} inbuf;

	spin_lock(&fl->hlock);
	if (fl->dsp_process_state) {
		err = -EALREADY;
		ADSPRPC_ERR("Already in create dynamic process\n");
		spin_unlock(&fl->hlock);
		return err;
	}
	fl->dsp_process_state = PROCESS_CREATE_IS_INPROGRESS;
	spin_unlock(&fl->hlock);
	inbuf.pgid = fl->tgid;
	inbuf.namelen = strlen(current->comm) + 1;
	inbuf.filelen = init->filelen;
	fl->pd = 1;

	/* Check if file memory passed by userspace is valid */
	VERIFY(err, access_ok((void __user *)init->file, init->filelen));
	if (err)
		goto bail;
	if (init->filelen) {
		/* Map the shell file buffer to remote subsystem */
		mutex_lock(&fl->map_mutex);
		err = fastrpc_mmap_create(fl, init->filefd, 0,
			init->file, init->filelen, mflags, &file);
		if (file)
			file->is_filemap = true;
		mutex_unlock(&fl->map_mutex);
		if (err)
			goto bail;
	}
	inbuf.pageslen = 1;

	/* Untrusted apps are not allowed to offload to signedPD on DSP. */
	if (fl->untrusted_process) {
		VERIFY(err, uproc->attrs & FASTRPC_MODE_UNSIGNED_MODULE);
		if (err) {
			err = -ECONNREFUSED;
			ADSPRPC_ERR(
				"untrusted app trying to offload to signed remote process\n");
			goto bail;
		}
	}

	/* Disregard any privilege bits from userspace */
	uproc->attrs &= (~FASTRPC_MODE_PRIVILEGED);

	/*
	 * Check if the primary or supplementary group(s) of the process is
	 * one of the 'privileged' fastrpc GIDs stored in the device-tree.
	 */
	gid = sorted_lists_intersection(fl->gidlist.gids,
		fl->gidlist.gidcount, gfa.gidlist.gids, gfa.gidlist.gidcount);
	if (gid) {
		ADSPRPC_INFO("PID %d, GID %u is a privileged process\n",
				fl->tgid, gid);
		uproc->attrs |= FASTRPC_MODE_PRIVILEGED;
	}

	/*
	 * Userspace client should try to allocate the initial memory donated
	 * to remote subsystem as only the kernel and DSP should have access
	 * to that memory.
	 */
	VERIFY(err, !init->mem);
	if (err) {
		err = -EINVAL;
		ADSPRPC_ERR("donated memory allocated in userspace\n");
		goto bail;
	}
	/* Free any previous donated memory */
	spin_lock(&fl->hlock);
	locked = 1;
	if (fl->init_mem) {
		init_mem = fl->init_mem;
		fl->init_mem = NULL;
		spin_unlock(&fl->hlock);
		locked = 0;
		fastrpc_buf_free(init_mem, 0);
	}
	if (locked) {
		spin_unlock(&fl->hlock);
		locked = 0;
	}

	/* Allocate DMA buffer in kernel for donating to remote process
	 * Unsigned PD requires additional memory because of the
	 * additional static heap initialized within the process.
	 */
	if (uproc->attrs & FASTRPC_MODE_UNSIGNED_MODULE)
		dsp_userpd_memlen += 2*one_mb;
	memlen = ALIGN(max(dsp_userpd_memlen, init->filelen * 4), one_mb);
	imem_dma_attr = DMA_ATTR_EXEC_MAPPING |
					DMA_ATTR_DELAYED_UNMAP |
					DMA_ATTR_NO_KERNEL_MAPPING;
	err = fastrpc_buf_alloc(fl, memlen, imem_dma_attr, 0,
				INITMEM_BUF, &imem);
	if (err)
		goto bail;
	fl->init_mem = imem;

	/*
	 * Prepare remote arguments for dynamic process create
	 * call to remote subsystem.
	 */
	inbuf.pageslen = 1;
	ra[0].buf.pv = (void *)&inbuf;
	ra[0].buf.len = sizeof(inbuf);
	fds[0] = -1;

	ra[1].buf.pv = (void *)current->comm;
	ra[1].buf.len = inbuf.namelen;
	fds[1] = -1;

	ra[2].buf.pv = (void *)init->file;
	ra[2].buf.len = inbuf.filelen;
	fds[2] = init->filefd;

	pages[0].addr = imem->phys;
	pages[0].size = imem->size;
	ra[3].buf.pv = (void *)pages;
	ra[3].buf.len = 1 * sizeof(*pages);
	fds[3] = -1;

	inbuf.attrs = uproc->attrs;
	ra[4].buf.pv = (void *)&(inbuf.attrs);
	ra[4].buf.len = sizeof(inbuf.attrs);
	fds[4] = -1;

	inbuf.siglen = uproc->siglen;
	ra[5].buf.pv = (void *)&(inbuf.siglen);
	ra[5].buf.len = sizeof(inbuf.siglen);
	fds[5] = -1;

	ioctl.inv.handle = FASTRPC_STATIC_HANDLE_PROCESS_GROUP;
	/*
	 * Choose appropriate remote method ID depending on whether the
	 * HLOS process has any attributes enabled (like unsignedPD,
	 * critical process, adaptive QoS, CRC checks etc).
	 */
	ioctl.inv.sc = REMOTE_SCALARS_MAKE(6, 4, 0);
	if (uproc->attrs)
		ioctl.inv.sc = REMOTE_SCALARS_MAKE(7, 6, 0);
	ioctl.inv.pra = ra;
	ioctl.fds = fds;
	ioctl.attrs = NULL;
	ioctl.crc = NULL;
	ioctl.perf_kernel = NULL;
	ioctl.perf_dsp = NULL;
	ioctl.job = NULL;
	err = fastrpc_internal_invoke(fl, FASTRPC_MODE_PARALLEL, KERNEL_MSG_WITH_ZERO_PID, &ioctl);
	if (err)
		goto bail;
bail:
	/*
	 * Shell is loaded into the donated memory on remote subsystem. So, the
	 * original file buffer can be DMA unmapped. In case of a failure also,
	 * the mapping needs to be removed.
	 */
	if (file) {
		mutex_lock(&fl->map_mutex);
		fastrpc_mmap_free(file, 0);
		mutex_unlock(&fl->map_mutex);
	}

		spin_lock(&fl->hlock);
		locked = 1;
	if (err) {
		fl->dsp_process_state = PROCESS_CREATE_DEFAULT;
		if (!IS_ERR_OR_NULL(fl->init_mem)) {
			init_mem = fl->init_mem;
			fl->init_mem = NULL;
			spin_unlock(&fl->hlock);
			locked = 0;
			fastrpc_buf_free(init_mem, 0);
		}
	} else {
		fl->dsp_process_state = PROCESS_CREATE_SUCCESS;
	}
		if (locked) {
			spin_unlock(&fl->hlock);
			locked = 0;
	}
	return err;
}

/*
 * This function makes a call to create a thread group in the static
 * process on the remote subsystem.
 * Example: audio daemon 'adsprpcd' on audioPD on ADSP
 */
static int fastrpc_init_create_static_process(struct fastrpc_file *fl,
				struct fastrpc_ioctl_init *init)
{
	int err = 0, rh_hyp_done = 0;
	struct fastrpc_apps *me = &gfa;
	struct fastrpc_ioctl_invoke_async ioctl;
	struct smq_phy_page pages[1];
	struct fastrpc_mmap *mem = NULL;
	char *proc_name = NULL;
	remote_arg_t ra[3];
	uint64_t phys = 0;
	size_t size = 0;
	int fds[3];
	struct secure_vm *rhvm = &me->channel[fl->cid].rhvm;
	struct {
		int pgid;
		unsigned int namelen;
		unsigned int pageslen;
	} inbuf;

	if (fl->dev_minor == MINOR_NUM_DEV) {
		err = -ECONNREFUSED;
		ADSPRPC_ERR(
			"untrusted app trying to attach to audio PD\n");
		return err;
	}

	if (!init->filelen)
		goto bail;

	proc_name = kzalloc(init->filelen + 1, GFP_KERNEL);
	VERIFY(err, !IS_ERR_OR_NULL(proc_name));
	if (err) {
		err = -ENOMEM;
		goto bail;
	}
	err = copy_from_user((void *)proc_name,
		(void __user *)init->file, init->filelen);
	if (err) {
		err = -EFAULT;
		goto bail;
	}

	fl->pd = 1;
	inbuf.pgid = fl->tgid;
	inbuf.namelen = init->filelen;
	inbuf.pageslen = 0;

	if (!strcmp(proc_name, "audiopd")) {
		fl->servloc_name = AUDIO_PDR_SERVICE_LOCATION_CLIENT_NAME;
		/*
		 * Remove any previous mappings in case process is trying
		 * to reconnect after a PD restart on remote subsystem.
		 */
		err = fastrpc_mmap_remove_pdr(fl);
		if (err)
			goto bail;
	} else {
		ADSPRPC_ERR(
			"Create static process is failed for proc_name %s",
			proc_name);
		goto bail;
	}

	if (!me->staticpd_flags && !me->legacy_remote_heap) {
		inbuf.pageslen = 1;
		if (!fastrpc_get_persistent_map(init->memlen, &mem)) {
			mutex_lock(&fl->map_mutex);
			err = fastrpc_mmap_create(fl, -1, 0, init->mem,
				 init->memlen, ADSP_MMAP_REMOTE_HEAP_ADDR, &mem);
			if (mem)
				mem->is_filemap = true;
			mutex_unlock(&fl->map_mutex);
			if (err)
				goto bail;
			spin_lock(&me->hlock);
			mem->in_use = true;
			spin_unlock(&me->hlock);
		}
		phys = mem->phys;
		size = mem->size;
		/*
		 * If remote-heap VMIDs are defined in DTSI, then do
		 * hyp_assign from HLOS to those VMs (LPASS, ADSP).
		 */
		if (rhvm->vmid && mem && !mem->is_persistent && mem->refs == 1 && size) {
			err = hyp_assign_phys(phys, (uint64_t)size,
				hlosvm, 1,
				rhvm->vmid, rhvm->vmperm, rhvm->vmcount);
			if (err) {
				ADSPRPC_ERR(
					"rh hyp assign failed with %d for phys 0x%llx, size %zu\n",
					err, phys, size);
				err = -EADDRNOTAVAIL;
				goto bail;
			}
			rh_hyp_done = 1;
		}
		me->staticpd_flags = 1;
		mem->is_persistent = true;
	}

	/*
	 * Prepare remote arguments for static process create
	 * call to remote subsystem.
	 */
	ra[0].buf.pv = (void *)&inbuf;
	ra[0].buf.len = sizeof(inbuf);
	fds[0] = -1;

	ra[1].buf.pv = (void *)proc_name;
	ra[1].buf.len = inbuf.namelen;
	fds[1] = -1;

	pages[0].addr = phys;
	pages[0].size = size;

	ra[2].buf.pv = (void *)pages;
	ra[2].buf.len = sizeof(*pages);
	fds[2] = -1;
	ioctl.inv.handle = FASTRPC_STATIC_HANDLE_PROCESS_GROUP;

	ioctl.inv.sc = REMOTE_SCALARS_MAKE(8, 3, 0);
	ioctl.inv.pra = ra;
	ioctl.fds = NULL;
	ioctl.attrs = NULL;
	ioctl.crc = NULL;
	ioctl.perf_kernel = NULL;
	ioctl.perf_dsp = NULL;
	ioctl.job = NULL;
	err = fastrpc_internal_invoke(fl, FASTRPC_MODE_PARALLEL, KERNEL_MSG_WITH_ZERO_PID, &ioctl);
	if (err)
		goto bail;
bail:
	kfree(proc_name);
	if (err) {
		me->staticpd_flags = 0;
		if (rh_hyp_done) {
			int hyp_err = 0;

			/* Assign memory back to HLOS in case of errors */
			hyp_err = hyp_assign_phys(phys, (uint64_t)size,
					rhvm->vmid, rhvm->vmcount,
					hlosvm, hlosvmperm, 1);
			if (hyp_err)
				ADSPRPC_WARN(
					"rh hyp unassign failed with %d for phys 0x%llx of size %zu\n",
					hyp_err, phys, size);
		}
		mutex_lock(&fl->map_mutex);
		fastrpc_mmap_free(mem, 0);
		mutex_unlock(&fl->map_mutex);
	}
	return err;
}

static int fastrpc_init_process(struct fastrpc_file *fl,
				struct fastrpc_ioctl_init_attrs *uproc)
{
	int err = 0;
	struct fastrpc_ioctl_init *init = &uproc->init;
	int cid = fl->cid;
	struct fastrpc_apps *me = &gfa;
	struct fastrpc_channel_ctx *chan = &me->channel[cid];

	VERIFY(err, init->filelen < INIT_FILELEN_MAX
			&& init->memlen < INIT_MEMLEN_MAX);
	if (err)
		goto bail;
	if (err) {
		ADSPRPC_ERR(
			"file size 0x%x or init memory 0x%x is more than max allowed file size 0x%x or init len 0x%x\n",
			init->filelen, init->memlen,
			INIT_FILELEN_MAX, INIT_MEMLEN_MAX);
		err = -EFBIG;
		goto bail;
	}

	if (chan->unsigned_support && fl->dev_minor == MINOR_NUM_DEV) {
		/* Make sure third party applications */
		/* can spawn only unsigned PD when */
		/* channel configured as secure. */
		if (chan->secure && !(uproc->attrs & FASTRPC_MODE_UNSIGNED_MODULE)) {
			err = -ECONNREFUSED;
			goto bail;
		}
	} else if (!(chan->unsigned_support) && (uproc->attrs & FASTRPC_MODE_UNSIGNED_MODULE)) {
		err = -ECONNREFUSED;
		goto bail;
	}
	err = fastrpc_channel_open(fl);
	if (err)
		goto bail;
	switch (init->flags) {
	case FASTRPC_INIT_ATTACH:
	case FASTRPC_INIT_ATTACH_SENSORS:
		err = fastrpc_init_attach_process(fl, init);
		break;
	case FASTRPC_INIT_CREATE:
		err = fastrpc_init_create_dynamic_process(fl, uproc);
		break;
	case FASTRPC_INIT_CREATE_STATIC:
		err = fastrpc_init_create_static_process(fl, init);
		break;
	default:
		err = -ENOTTY;
		break;
	}
	if (err)
		goto bail;
	fl->dsp_proc_init = 1;
bail:
	return err;
}

static int fastrpc_send_cpuinfo_to_dsp(struct fastrpc_file *fl)
{
	int err = 0, cid = -1;
	uint64_t cpuinfo = 0;
	struct fastrpc_apps *me = &gfa;
	struct fastrpc_ioctl_invoke_async ioctl;
	remote_arg_t ra[2];

	if (!fl) {
		err = -EBADF;
		goto bail;
	}
	cid = fl->cid;
	VERIFY(err, VALID_FASTRPC_CID(cid));
	if (err) {
		err = -ECHRNG;
		ADSPRPC_ERR(
			"invalid channel 0x%zx set for session\n",
			cid);
		goto bail;
	}

	cpuinfo = me->channel[cid].cpuinfo_todsp;
	/* return success if already updated to remote processor */
	if (me->channel[cid].cpuinfo_status)
		return 0;

	ra[0].buf.pv = (void *)&cpuinfo;
	ra[0].buf.len = sizeof(cpuinfo);
	ioctl.inv.handle = FASTRPC_STATIC_HANDLE_DSP_UTILITIES;
	ioctl.inv.sc = REMOTE_SCALARS_MAKE(1, 1, 0);
	ioctl.inv.pra = ra;
	ioctl.fds = NULL;
	ioctl.attrs = NULL;
	ioctl.crc = NULL;
	ioctl.perf_kernel = NULL;
	ioctl.perf_dsp = NULL;
	ioctl.job = NULL;
	fl->pd = 1;

	err = fastrpc_internal_invoke(fl, FASTRPC_MODE_PARALLEL, KERNEL_MSG_WITH_ZERO_PID, &ioctl);
	if (!err)
		me->channel[cid].cpuinfo_status = true;
bail:
	return err;
}

static int fastrpc_get_info_from_dsp(struct fastrpc_file *fl,
				uint32_t *dsp_attr_buf,
				uint32_t dsp_attr_buf_len,
				uint32_t domain)
{
	int err = 0;
	struct fastrpc_ioctl_invoke_async ioctl;
	remote_arg_t ra[2];

	dsp_attr_buf[0] = 0;	// Capability filled in userspace

	// Fastrpc to modem not supported
	if (domain == MDSP_DOMAIN_ID)
		goto bail;

	err = fastrpc_channel_open(fl);
	if (err)
		goto bail;

	ra[0].buf.pv = (void *)&dsp_attr_buf_len;
	ra[0].buf.len = sizeof(dsp_attr_buf_len);
	ra[1].buf.pv = (void *)(&dsp_attr_buf[1]);
	ra[1].buf.len = dsp_attr_buf_len * sizeof(uint32_t);
	ioctl.inv.handle = FASTRPC_STATIC_HANDLE_DSP_UTILITIES;
	ioctl.inv.sc = REMOTE_SCALARS_MAKE(0, 1, 1);
	ioctl.inv.pra = ra;
	ioctl.fds = NULL;
	ioctl.attrs = NULL;
	ioctl.crc = NULL;
	ioctl.perf_kernel = NULL;
	ioctl.perf_dsp = NULL;
	ioctl.job = NULL;
	fl->pd = 1;

	err = fastrpc_internal_invoke(fl, FASTRPC_MODE_PARALLEL, KERNEL_MSG_WITH_ZERO_PID, &ioctl);
bail:

	if (err)
		ADSPRPC_ERR("could not obtain dsp information, err val %d\n",
		err);
	return err;
}

static int fastrpc_get_info_from_kernel(
		struct fastrpc_ioctl_capability *cap,
		struct fastrpc_file *fl)
{
	int err = 0;
	uint32_t domain = cap->domain, attribute_ID = cap->attribute_ID;
	uint32_t async_capability = 0;
	struct fastrpc_dsp_capabilities *dsp_cap_ptr = NULL;

	VERIFY(err, domain < NUM_CHANNELS);
	if (err) {
		err = -ECHRNG;
		goto bail;
	}

	/*
	 * Check if number of attribute IDs obtained from userspace
	 * is less than the number of attribute IDs supported by
	 * kernel
	 */
	if (attribute_ID >= FASTRPC_MAX_ATTRIBUTES) {
		err = -EOVERFLOW;
		goto bail;
	}

	dsp_cap_ptr = &gcinfo[domain].dsp_cap_kernel;

	if (attribute_ID >= FASTRPC_MAX_DSP_ATTRIBUTES) {
		// Driver capability, pass it to user
		memcpy(&cap->capability,
			&kernel_capabilities[attribute_ID -
			FASTRPC_MAX_DSP_ATTRIBUTES],
			sizeof(cap->capability));
	} else if (!dsp_cap_ptr->is_cached) {
		/*
		 * Information not on kernel, query device for information
		 * and cache on kernel
		 */
		err = fastrpc_get_info_from_dsp(fl,
			  dsp_cap_ptr->dsp_attributes,
			  FASTRPC_MAX_DSP_ATTRIBUTES - 1,
			  domain);
		if (err)
			goto bail;

		/* Async capability support depends on both kernel and DSP */
		async_capability = IS_ASYNC_FASTRPC_AVAILABLE &&
			dsp_cap_ptr->dsp_attributes[ASYNC_FASTRPC_CAP];
		dsp_cap_ptr->dsp_attributes[ASYNC_FASTRPC_CAP]
			= async_capability;
		memcpy(&cap->capability,
			&dsp_cap_ptr->dsp_attributes[attribute_ID],
			sizeof(cap->capability));

		dsp_cap_ptr->is_cached = 1;
	} else {
		// Information on Kernel, pass it to user
		memcpy(&cap->capability,
			&dsp_cap_ptr->dsp_attributes[attribute_ID],
			sizeof(cap->capability));
	}
bail:
	return err;
}

static int fastrpc_release_current_dsp_process(struct fastrpc_file *fl)
{
	int err = 0, cid = -1;
	struct fastrpc_ioctl_invoke_async ioctl;
	remote_arg_t ra[1];
	int tgid = 0;

	if (!fl) {
		err = -EBADF;
		goto bail;
	}
	cid = fl->cid;
	VERIFY(err, VALID_FASTRPC_CID(cid));
	if (err) {
		err = -ECHRNG;
		goto bail;
	}
	VERIFY(err, fl->sctx != NULL);
	if (err) {
		err = -EBADR;
		goto bail;
	}
	VERIFY(err, fl->apps->channel[cid].rpdev != NULL);
	if (err) {
		err = -ENODEV;
		goto bail;
	}
	VERIFY(err, fl->apps->channel[cid].issubsystemup == 1);
	if (err) {
		err = -ECONNRESET;
		goto bail;
	}
	tgid = fl->tgid;
	ra[0].buf.pv = (void *)&tgid;
	ra[0].buf.len = sizeof(tgid);
	ioctl.inv.handle = FASTRPC_STATIC_HANDLE_PROCESS_GROUP;
	ioctl.inv.sc = REMOTE_SCALARS_MAKE(1, 1, 0);
	ioctl.inv.pra = ra;
	ioctl.fds = NULL;
	ioctl.attrs = NULL;
	ioctl.crc = NULL;
	ioctl.perf_kernel = NULL;
	ioctl.perf_dsp = NULL;
	ioctl.job = NULL;
	spin_lock(&fl->apps->hlock);
	fl->file_close = FASTRPC_PROCESS_DSP_EXIT_INIT;
	spin_unlock(&fl->apps->hlock);
	/*
	 * Pass 2 for "kernel" arg to send kernel msg to DSP
	 * with non-zero msg PID for the DSP to directly use
	 * that info to kill the remote process.
	 */
	VERIFY(err, 0 == (err = fastrpc_internal_invoke(fl,
		FASTRPC_MODE_PARALLEL, KERNEL_MSG_WITH_NONZERO_PID, &ioctl)));
	spin_lock(&fl->apps->hlock);
	fl->file_close = FASTRPC_PROCESS_DSP_EXIT_COMPLETE;
	spin_unlock(&fl->apps->hlock);
	if (err && fl->dsp_proc_init)
		ADSPRPC_ERR(
			"releasing DSP process failed with %d (0x%x) for %s\n",
			err, err, current->comm);
bail:
	return err;
}

static int fastrpc_mem_map_to_dsp(struct fastrpc_file *fl, int fd, int offset,
				uint32_t flags, uintptr_t va, uint64_t phys,
				size_t size, uintptr_t *raddr)
{
	struct fastrpc_ioctl_invoke_async ioctl;
	struct smq_phy_page page;
	remote_arg_t ra[4];
	int err = 0;
	struct {
		int pid;
		int fd;
		int offset;
		uint32_t flags;
		uint64_t vaddrin;
		int num;
		int data_len;
	} inargs;
	struct {
		uint64_t vaddrout;
	} routargs;

	inargs.pid = fl->tgid;
	inargs.fd = fd;
	inargs.offset = offset;
	inargs.vaddrin = (uintptr_t)va;
	inargs.flags = flags;
	inargs.num = sizeof(page);
	inargs.data_len = 0;
	ra[0].buf.pv = (void *)&inargs;
	ra[0].buf.len = sizeof(inargs);
	page.addr = phys;
	page.size = size;
	ra[1].buf.pv = (void *)&page;
	ra[1].buf.len = sizeof(page);
	ra[2].buf.pv = (void *)&page;
	ra[2].buf.len = 0;
	ra[3].buf.pv = (void *)&routargs;
	ra[3].buf.len = sizeof(routargs);

	ioctl.inv.handle = FASTRPC_STATIC_HANDLE_PROCESS_GROUP;
	ioctl.inv.sc = REMOTE_SCALARS_MAKE(10, 3, 1);
	ioctl.inv.pra = ra;
	ioctl.fds = NULL;
	ioctl.attrs = NULL;
	ioctl.crc = NULL;
	ioctl.perf_kernel = NULL;
	ioctl.perf_dsp = NULL;
	ioctl.job = NULL;
	VERIFY(err, 0 == (err = fastrpc_internal_invoke(fl,
		FASTRPC_MODE_PARALLEL, KERNEL_MSG_WITH_ZERO_PID, &ioctl)));
	if (err)
		goto bail;
	if (raddr)
		*raddr = (uintptr_t)routargs.vaddrout;
bail:
	if (err) {
		pr_err("adsprpc: %s failed. err 0x%x fd %d len 0x%x\n",
			__func__, err, fd, size);
	}
	return err;
}

static int fastrpc_mem_unmap_to_dsp(struct fastrpc_file *fl, int fd,
				uint32_t flags,	uintptr_t va,
				uint64_t phys, size_t size)
{
	struct fastrpc_ioctl_invoke_async ioctl;
	remote_arg_t ra[1];
	int err = 0;
	struct {
		int pid;
		int fd;
		uint64_t vaddrin;
		uint64_t len;
	} inargs;

	inargs.pid = fl->tgid;
	inargs.fd = fd;
	inargs.vaddrin = (uint64_t)va;
	inargs.len = (uint64_t)size;
	ra[0].buf.pv = (void *)&inargs;
	ra[0].buf.len = sizeof(inargs);

	ioctl.inv.handle = FASTRPC_STATIC_HANDLE_PROCESS_GROUP;
	ioctl.inv.sc = REMOTE_SCALARS_MAKE(11, 1, 0);
	ioctl.inv.pra = ra;
	ioctl.fds = NULL;
	ioctl.attrs = NULL;
	ioctl.crc = NULL;
	ioctl.perf_kernel = NULL;
	ioctl.perf_dsp = NULL;
	ioctl.job = NULL;
	VERIFY(err, 0 == (err = fastrpc_internal_invoke(fl,
		FASTRPC_MODE_PARALLEL, KERNEL_MSG_WITH_ZERO_PID, &ioctl)));
	if (err)
		goto bail;
bail:
	if (err) {
		pr_err("adsprpc: %s failed. err 0x%x fd %d len 0x%x\n",
			__func__, err, fd, size);
	}
	return err;
}

static int fastrpc_unmap_on_dsp(struct fastrpc_file *fl,
		uintptr_t raddr, uint64_t phys, size_t size, uint32_t flags)
{
	struct fastrpc_ioctl_invoke_async ioctl;
	remote_arg_t ra[1] = {};
	int err = 0;
	struct {
		int pid;
		uintptr_t vaddrout;
		size_t size;
	} inargs;

	inargs.pid = fl->tgid;
	inargs.size = size;
	inargs.vaddrout = raddr;
	ra[0].buf.pv = (void *)&inargs;
	ra[0].buf.len = sizeof(inargs);

	ioctl.inv.handle = FASTRPC_STATIC_HANDLE_PROCESS_GROUP;
	if (fl->apps->compat)
		ioctl.inv.sc = REMOTE_SCALARS_MAKE(5, 1, 0);
	else
		ioctl.inv.sc = REMOTE_SCALARS_MAKE(3, 1, 0);
	ioctl.inv.pra = ra;
	ioctl.fds = NULL;
	ioctl.attrs = NULL;
	ioctl.crc = NULL;
	ioctl.perf_kernel = NULL;
	ioctl.perf_dsp = NULL;
	ioctl.job = NULL;
	VERIFY(err, 0 == (err = fastrpc_internal_invoke(fl,
		FASTRPC_MODE_PARALLEL, KERNEL_MSG_WITH_ZERO_PID, &ioctl)));
	if (err)
		goto bail;
bail:
	return err;
}

static int fastrpc_mmap_on_dsp(struct fastrpc_file *fl, uint32_t flags,
					uintptr_t va, uint64_t phys,
					size_t size, int refs, uintptr_t *raddr)
{
	struct fastrpc_ioctl_invoke_async ioctl;
	struct fastrpc_apps *me = &gfa;
	struct smq_phy_page page;
	int num = 1, cid = -1;
	remote_arg_t ra[3];
	int err = 0;
	struct {
		int pid;
		uint32_t flags;
		uintptr_t vaddrin;
		int num;
	} inargs;
	struct {
		uintptr_t vaddrout;
	} routargs;

	if (!fl) {
		err = -EBADF;
		goto bail;
	}
	cid = fl->cid;
	inargs.pid = fl->tgid;
	inargs.vaddrin = (uintptr_t)va;
	inargs.flags = flags;
	inargs.num = fl->apps->compat ? num * sizeof(page) : num;
	ra[0].buf.pv = (void *)&inargs;
	ra[0].buf.len = sizeof(inargs);
	page.addr = phys;
	page.size = size;
	ra[1].buf.pv = (void *)&page;
	ra[1].buf.len = num * sizeof(page);

	ra[2].buf.pv = (void *)&routargs;
	ra[2].buf.len = sizeof(routargs);

	ioctl.inv.handle = FASTRPC_STATIC_HANDLE_PROCESS_GROUP;
	if (fl->apps->compat)
		ioctl.inv.sc = REMOTE_SCALARS_MAKE(4, 2, 1);
	else
		ioctl.inv.sc = REMOTE_SCALARS_MAKE(2, 2, 1);
	ioctl.inv.pra = ra;
	ioctl.fds = NULL;
	ioctl.attrs = NULL;
	ioctl.crc = NULL;
	ioctl.perf_kernel = NULL;
	ioctl.perf_dsp = NULL;
	ioctl.job = NULL;
	VERIFY(err, 0 == (err = fastrpc_internal_invoke(fl,
		FASTRPC_MODE_PARALLEL, KERNEL_MSG_WITH_ZERO_PID, &ioctl)));
	*raddr = (uintptr_t)routargs.vaddrout;
	if (err)
		goto bail;
	if (flags == ADSP_MMAP_REMOTE_HEAP_ADDR) {
		VERIFY(err, VALID_FASTRPC_CID(cid));
		if (err) {
			err = -ECHRNG;
			ADSPRPC_ERR(
				"invalid channel 0x%zx set for session\n",
				cid);
			goto bail;
		}
	}
	if (flags == ADSP_MMAP_REMOTE_HEAP_ADDR
				&& me->channel[cid].rhvm.vmid && refs == 1) {
		err = hyp_assign_phys(phys, (uint64_t)size,
				hlosvm, 1, me->channel[cid].rhvm.vmid,
				me->channel[cid].rhvm.vmperm,
				me->channel[cid].rhvm.vmcount);
		if (err) {
			ADSPRPC_ERR(
				"rh hyp assign failed with %d for phys 0x%llx, size %zu\n",
				err, phys, size);
			err = -EADDRNOTAVAIL;
			err = fastrpc_unmap_on_dsp(fl,
				*raddr, phys, size, flags);
			if (err) {
				ADSPRPC_ERR(
					"failed to unmap %d for phys 0x%llx, size %zd\n",
					err, phys, size);
			}
			goto bail;
		}
	}
bail:
	return err;
}

static int fastrpc_munmap_on_dsp_rh(struct fastrpc_file *fl, uint64_t phys,
						size_t size, uint32_t flags)
{
	int err = 0, cid = -1;
	struct fastrpc_apps *me = &gfa;
	int tgid = 0;
	int destVM[1] = {VMID_HLOS};
	int destVMperm[1] = {PERM_READ | PERM_WRITE | PERM_EXEC};

	if (flags == ADSP_MMAP_HEAP_ADDR) {
		struct fastrpc_ioctl_invoke_async ioctl;
		remote_arg_t ra[2];
		int err = 0;
		struct {
			uint8_t skey;
		} routargs;

		if (!fl) {
			err = -EBADF;
			goto bail;
		}

		cid = fl->cid;
		VERIFY(err, VALID_FASTRPC_CID(cid));
		if (err) {
			err = -ECHRNG;
			ADSPRPC_ERR(
				"invalid channel 0x%zx set for session\n",
				cid);
			goto bail;
		}
		tgid = fl->tgid;
		ra[0].buf.pv = (void *)&tgid;
		ra[0].buf.len = sizeof(tgid);

		ra[1].buf.pv = (void *)&routargs;
		ra[1].buf.len = sizeof(routargs);

		ioctl.inv.handle = FASTRPC_STATIC_HANDLE_PROCESS_GROUP;
		ioctl.inv.sc = REMOTE_SCALARS_MAKE(9, 1, 1);
		ioctl.inv.pra = ra;
		ioctl.fds = NULL;
		ioctl.attrs = NULL;
		ioctl.crc = NULL;
		ioctl.perf_kernel = NULL;
		ioctl.perf_dsp = NULL;
		ioctl.job = NULL;

		VERIFY(err, 0 == (err = fastrpc_internal_invoke(fl,
				FASTRPC_MODE_PARALLEL, KERNEL_MSG_WITH_ZERO_PID, &ioctl)));
		if (err)
			goto bail;
	} else if (flags == ADSP_MMAP_REMOTE_HEAP_ADDR) {
		if (me->channel[fl->cid].rhvm.vmid) {
			err = hyp_assign_phys(phys,
					(uint64_t)size,
					me->channel[fl->cid].rhvm.vmid,
					me->channel[fl->cid].rhvm.vmcount,
					destVM, destVMperm, 1);
			if (err) {
				ADSPRPC_ERR(
					"rh hyp unassign failed with %d for phys 0x%llx, size %zu\n",
					err, phys, size);
				err = -EADDRNOTAVAIL;
				goto bail;
			}
		}
	}

bail:
	return err;
}

static int fastrpc_munmap_on_dsp(struct fastrpc_file *fl, uintptr_t raddr,
				uint64_t phys, size_t size, uint32_t flags)
{
	int err = 0;

	VERIFY(err, 0 == (err = fastrpc_unmap_on_dsp(fl, raddr, phys,
						size, flags)));
	if (err)
		goto bail;
	if (flags == ADSP_MMAP_HEAP_ADDR ||
				flags == ADSP_MMAP_REMOTE_HEAP_ADDR) {
		VERIFY(err, !(err = fastrpc_munmap_on_dsp_rh(fl, phys,
			size, flags)));
		if (err)
			goto bail;
	}
bail:
	return err;
}

static int fastrpc_mmap_remove_ssr(struct fastrpc_file *fl)
{
	struct fastrpc_mmap *match = NULL, *map = NULL;
	struct hlist_node *n = NULL;
	int err = 0, ret = 0;
	struct fastrpc_apps *me = &gfa;
	struct ramdump_segment ramdump_segments_rh;

	VERIFY(err, fl->cid == RH_CID);
	if (err) {
		err = -EBADR;
		goto bail;
	}
	do {
		match = NULL;
		spin_lock(&me->hlock);
		hlist_for_each_entry_safe(map, n, &me->maps, hn) {
			match = map;
			if (map->is_persistent) {
				map->in_use = false;
				match = NULL;
				continue;
			}
			hlist_del_init(&map->hn);
			break;
		}
		spin_unlock(&me->hlock);

		if (match) {
			err = fastrpc_munmap_on_dsp_rh(fl, match->phys,
						match->size, match->flags);
			if (err)
				goto bail;
			if (me->ramdump_handle && me->enable_ramdump) {
				ramdump_segments_rh.address =
				match->phys;
				ramdump_segments_rh.v_address =
				(void __iomem *)match->va;
				ramdump_segments_rh.size = match->size;
				ret = fastrpc_elf_ramdump(me->ramdump_handle, &ramdump_segments_rh);
				if (ret < 0)
					pr_err("adsprpc: %s: unable to dump heap (err %d)\n",
						__func__, ret);
			}
			fastrpc_mmap_free(match, 0);
		}
	} while (match);
	me->enable_ramdump = false;
bail:
	if (err && match)
		fastrpc_mmap_add(match);
	return err;
}

static int fastrpc_mmap_remove_pdr(struct fastrpc_file *fl)
{
	struct fastrpc_apps *me = &gfa;
	int session = 0, err = 0, cid = -1;

	if (!fl) {
		err = -EBADF;
		goto bail;
	}
	err = fastrpc_get_spd_session(fl->servloc_name,
			&session, &cid);
	if (err)
		goto bail;
	VERIFY(err, cid == fl->cid);
	if (err) {
		err = -EBADR;
		goto bail;
	}
	if (!me->channel[cid].spd[session].ispdup) {
		err = -ENOTCONN;
		goto bail;
	}
	if (me->channel[cid].spd[session].pdrcount !=
		me->channel[cid].spd[session].prevpdrcount) {
		err = fastrpc_mmap_remove_ssr(fl);
		if (err)
			ADSPRPC_WARN("failed to unmap remote heap (err %d)\n",
					err);
		me->channel[cid].spd[session].prevpdrcount =
				me->channel[cid].spd[session].pdrcount;
	}
bail:
	return err;
}

static inline void get_fastrpc_ioctl_mmap_64(
			struct fastrpc_ioctl_mmap_64 *mmap64,
			struct fastrpc_ioctl_mmap *immap)
{
	immap->fd = mmap64->fd;
	immap->flags = mmap64->flags;
	immap->vaddrin = (uintptr_t)mmap64->vaddrin;
	immap->size = mmap64->size;
}

static inline void put_fastrpc_ioctl_mmap_64(
			struct fastrpc_ioctl_mmap_64 *mmap64,
			struct fastrpc_ioctl_mmap *immap)
{
	mmap64->vaddrout = (uint64_t)immap->vaddrout;
}

static inline void get_fastrpc_ioctl_munmap_64(
			struct fastrpc_ioctl_munmap_64 *munmap64,
			struct fastrpc_ioctl_munmap *imunmap)
{
	imunmap->vaddrout = (uintptr_t)munmap64->vaddrout;
	imunmap->size = munmap64->size;
}

static int fastrpc_internal_munmap(struct fastrpc_file *fl,
				   struct fastrpc_ioctl_munmap *ud)
{
	int err = 0;
	struct fastrpc_mmap *map = NULL;
	struct fastrpc_buf *rbuf = NULL, *free = NULL;
	struct hlist_node *n;

	VERIFY(err, fl->dsp_proc_init == 1);
	if (err) {
		ADSPRPC_ERR(
			"user application %s trying to unmap without initialization\n",
			current->comm);
		err = -EHOSTDOWN;
		goto bail;
	}
	mutex_lock(&fl->internal_map_mutex);

	spin_lock(&fl->hlock);
	hlist_for_each_entry_safe(rbuf, n, &fl->remote_bufs, hn_rem) {
		if (rbuf->raddr && ((rbuf->flags == ADSP_MMAP_ADD_PAGES) ||
				    (rbuf->flags == ADSP_MMAP_ADD_PAGES_LLC))) {
			if ((rbuf->raddr == ud->vaddrout) &&
				(rbuf->size == ud->size)) {
				free = rbuf;
				break;
			}
		}
	}
	spin_unlock(&fl->hlock);

	if (free) {
		VERIFY(err, !(err = fastrpc_munmap_on_dsp(fl, free->raddr,
			free->phys, free->size, free->flags)));
		if (err)
			goto bail;
		fastrpc_buf_free(rbuf, 0);
		mutex_unlock(&fl->internal_map_mutex);
		return err;
	}

	mutex_lock(&fl->map_mutex);
	VERIFY(err, !(err = fastrpc_mmap_remove(fl, -1, ud->vaddrout,
		ud->size, &map)));
	mutex_unlock(&fl->map_mutex);
	if (err)
		goto bail;
	VERIFY(err, map != NULL);
	if (err) {
		err = -EINVAL;
		goto bail;
	}
	if (!map->is_persistent) {
		VERIFY(err, !(err = fastrpc_munmap_on_dsp(fl, map->raddr,
					map->phys, map->size, map->flags)));
	}

	if (err)
		goto bail;
	mutex_lock(&fl->map_mutex);
	fastrpc_mmap_free(map, 0);
	mutex_unlock(&fl->map_mutex);
bail:
	if (err && map) {
		mutex_lock(&fl->map_mutex);
		fastrpc_mmap_add(map);
		mutex_unlock(&fl->map_mutex);
	}
	mutex_unlock(&fl->internal_map_mutex);
	return err;
}

/*
 *	fastrpc_internal_munmap_fd can only be used for buffers
 *	mapped with persist attributes. This can only be called
 *	once for any persist buffer
 */
static int fastrpc_internal_munmap_fd(struct fastrpc_file *fl,
				struct fastrpc_ioctl_munmap_fd *ud)
{
	int err = 0;
	struct fastrpc_mmap *map = NULL;

	VERIFY(err, (fl && ud));
	if (err) {
		err = -EINVAL;
		return err;
	}
	VERIFY(err, fl->dsp_proc_init == 1);
	if (err) {
		ADSPRPC_ERR(
			"user application %s trying to unmap without initialization\n",
			current->comm);
		err = -EHOSTDOWN;
		return err;
	}
	mutex_lock(&fl->internal_map_mutex);
	mutex_lock(&fl->map_mutex);
	err = fastrpc_mmap_find(fl, ud->fd, ud->va, ud->len, 0, 0, &map);
	if (err) {
		ADSPRPC_ERR(
			"mapping not found to unmap fd 0x%x, va 0x%llx, len 0x%x, err %d\n",
			ud->fd, (unsigned long long)ud->va,
			(unsigned int)ud->len, err);
		mutex_unlock(&fl->map_mutex);
		goto bail;
	}
	if (map && (map->attr & FASTRPC_ATTR_KEEP_MAP)) {
		map->attr = map->attr & (~FASTRPC_ATTR_KEEP_MAP);
		fastrpc_mmap_free(map, 0);
	}
	mutex_unlock(&fl->map_mutex);
bail:
	mutex_unlock(&fl->internal_map_mutex);
	return err;
}

static int fastrpc_internal_mem_map(struct fastrpc_file *fl,
				struct fastrpc_ioctl_mem_map *ud)
{
	int err = 0;
	struct fastrpc_mmap *map = NULL;

	mutex_lock(&fl->internal_map_mutex);
	VERIFY(err, fl->dsp_proc_init == 1);
	if (err) {
		pr_err("adsprpc: ERROR: %s: user application %s trying to map without initialization\n",
			__func__, current->comm);
		err = EBADR;
		goto bail;
	}

	/* create SMMU mapping */
	mutex_lock(&fl->map_mutex);
	VERIFY(err, !fastrpc_mmap_create(fl, ud->m.fd, ud->m.attrs,
			ud->m.vaddrin, ud->m.length,
			 ud->m.flags, &map));
	mutex_unlock(&fl->map_mutex);
	if (err)
		goto bail;

	if (map->raddr) {
		err = -EEXIST;
		goto bail;
	}

	/* create DSP mapping */
	VERIFY(err, !(err = fastrpc_mem_map_to_dsp(fl, ud->m.fd, ud->m.offset,
		ud->m.flags, map->va, map->phys, map->size, &map->raddr)));
	if (err)
		goto bail;
	ud->m.vaddrout = map->raddr;
bail:
	if (err) {
		pr_err("adsprpc: %s failed to map fd %d flags %d err %d\n",
			__func__, ud->m.fd, ud->m.flags, err);
		if (map) {
			mutex_lock(&fl->map_mutex);
			fastrpc_mmap_free(map, 0);
			mutex_unlock(&fl->map_mutex);
		}
	}
	mutex_unlock(&fl->internal_map_mutex);
	return err;
}

static int fastrpc_internal_mem_unmap(struct fastrpc_file *fl,
				struct fastrpc_ioctl_mem_unmap *ud)
{
	int err = 0;
	struct fastrpc_mmap *map = NULL;

	mutex_lock(&fl->internal_map_mutex);
	VERIFY(err, fl->dsp_proc_init == 1);
	if (err) {
		pr_err("adsprpc: ERROR: %s: user application %s trying to map without initialization\n",
			__func__, current->comm);
		err = EBADR;
		goto bail;
	}

	mutex_lock(&fl->map_mutex);
	VERIFY(err, !fastrpc_mmap_remove(fl, ud->um.fd,
			(uintptr_t)ud->um.vaddr, ud->um.length, &map));
	mutex_unlock(&fl->map_mutex);
	if (err)
		goto bail;

	VERIFY(err, map->flags == FASTRPC_MAP_FD ||
		map->flags == FASTRPC_MAP_FD_DELAYED ||
		map->flags == FASTRPC_MAP_STATIC);
	if (err) {
		err = -EBADMSG;
		goto bail;
	}

	/* remove mapping on DSP */
	VERIFY(err, !(err = fastrpc_mem_unmap_to_dsp(fl, map->fd, map->flags,
				map->raddr, map->phys, map->size)));
	if (err)
		goto bail;

	/* remove SMMU mapping */
	mutex_lock(&fl->map_mutex);
	fastrpc_mmap_free(map, 0);
	mutex_unlock(&fl->map_mutex);
	map = NULL;
bail:
	if (err) {
		pr_err("adsprpc: %s failed to unmap fd %d addr 0x%llx length 0x%x err 0x%x\n",
			__func__, ud->um.fd, ud->um.vaddr, ud->um.length, err);
		/* Add back to map list in case of error to unmap on DSP */
		if (map) {
			mutex_lock(&fl->map_mutex);
			fastrpc_mmap_add(map);
			mutex_unlock(&fl->map_mutex);
		}
	}
	mutex_unlock(&fl->internal_map_mutex);
	return err;
}

static int fastrpc_internal_mmap(struct fastrpc_file *fl,
				 struct fastrpc_ioctl_mmap *ud)
{
	struct fastrpc_mmap *map = NULL;
	struct fastrpc_buf *rbuf = NULL;
	unsigned long dma_attr = 0;
	uintptr_t raddr = 0;
	int err = 0;

	VERIFY(err, fl->dsp_proc_init == 1);
	if (err) {
		ADSPRPC_ERR(
			"user application %s trying to map without initialization\n",
			current->comm);
		err = -EHOSTDOWN;
		return err;
	}
	mutex_lock(&fl->internal_map_mutex);
	if ((ud->flags == ADSP_MMAP_ADD_PAGES) ||
	    (ud->flags == ADSP_MMAP_ADD_PAGES_LLC)) {
		if (ud->vaddrin) {
			err = -EINVAL;
			ADSPRPC_ERR(
				"adding user allocated pages is not supported\n");
			goto bail;
		}
		dma_attr = DMA_ATTR_EXEC_MAPPING |
					DMA_ATTR_DELAYED_UNMAP |
					DMA_ATTR_NO_KERNEL_MAPPING;
		if (ud->flags == ADSP_MMAP_ADD_PAGES_LLC)
			dma_attr |= DMA_ATTR_IOMMU_USE_UPSTREAM_HINT;
		err = fastrpc_buf_alloc(fl, ud->size, dma_attr, ud->flags,
						USERHEAP_BUF, &rbuf);
		if (err)
			goto bail;
		err = fastrpc_mmap_on_dsp(fl, ud->flags, 0,
				rbuf->phys, rbuf->size, 0, &raddr);
		if (err)
			goto bail;
		rbuf->raddr = raddr;
	} else {
		uintptr_t va_to_dsp;

		mutex_lock(&fl->map_mutex);
		VERIFY(err, !(err = fastrpc_mmap_create(fl, ud->fd, 0,
				(uintptr_t)ud->vaddrin, ud->size,
				 ud->flags, &map)));
		mutex_unlock(&fl->map_mutex);
		if (err)
			goto bail;

		if (ud->flags == ADSP_MMAP_HEAP_ADDR ||
				ud->flags == ADSP_MMAP_REMOTE_HEAP_ADDR)
			va_to_dsp = 0;
		else
			va_to_dsp = (uintptr_t)map->va;
		VERIFY(err, 0 == (err = fastrpc_mmap_on_dsp(fl, ud->flags,
			va_to_dsp, map->phys, map->size, map->refs, &raddr)));
		if (err)
			goto bail;
		map->raddr = raddr;
	}
	ud->vaddrout = raddr;
 bail:
	if (err) {
		if (map) {
			mutex_lock(&fl->map_mutex);
			fastrpc_mmap_free(map, 0);
			mutex_unlock(&fl->map_mutex);
		}
		if (!IS_ERR_OR_NULL(rbuf))
			fastrpc_buf_free(rbuf, 0);
	}
	mutex_unlock(&fl->internal_map_mutex);
	return err;
}

static void fastrpc_context_list_dtor(struct fastrpc_file *fl);

static int fastrpc_session_alloc_locked(struct fastrpc_channel_ctx *chan,
			int secure, struct fastrpc_session_ctx **session)
{
	struct fastrpc_apps *me = &gfa;
	uint64_t idx = 0;
	int err = 0;

	if (chan->sesscount) {
		for (idx = 0; idx < chan->sesscount; ++idx) {
			if (!chan->session[idx].used &&
				chan->session[idx].smmu.secure == secure) {
				chan->session[idx].used = 1;
				break;
			}
		}
		if (idx >= chan->sesscount) {
			err = -EUSERS;
			ADSPRPC_ERR(
				"max concurrent sessions limit (%d) already reached on %s err %d\n",
				chan->sesscount, chan->subsys, err);
			goto bail;
		}
		chan->session[idx].smmu.faults = 0;
	} else {
		VERIFY(err, me->dev != NULL);
		if (err) {
			err = -ENODEV;
			goto bail;
		}
		chan->session[0].dev = me->dev;
		chan->session[0].smmu.dev = me->dev;
	}

	*session = &chan->session[idx];
 bail:
	return err;
}

static inline int get_cid_from_rpdev(struct rpmsg_device *rpdev)
{
	int err = 0, cid = -1;

	VERIFY(err, !IS_ERR_OR_NULL(rpdev));
	if (err)
		return -ENODEV;

	if (!strcmp(rpdev->dev.parent->of_node->name, "cdsp"))
		cid = CDSP_DOMAIN_ID;
	else if (!strcmp(rpdev->dev.parent->of_node->name, "adsp"))
		cid = ADSP_DOMAIN_ID;
	else if (!strcmp(rpdev->dev.parent->of_node->name, "dsps"))
		cid = SDSP_DOMAIN_ID;
	else if (!strcmp(rpdev->dev.parent->of_node->name, "mdsp"))
		cid = MDSP_DOMAIN_ID;

	return cid;
}

static int fastrpc_rpmsg_probe(struct rpmsg_device *rpdev)
{
	int err = 0;
	int cid = -1;

	VERIFY(err, !IS_ERR_OR_NULL(rpdev));
	if (err)
		return -ENODEV;

	cid = get_cid_from_rpdev(rpdev);
	VERIFY(err, cid >= ADSP_DOMAIN_ID && cid < NUM_CHANNELS);
	if (err) {
		err = -ECHRNG;
		goto bail;
	}
	mutex_lock(&gcinfo[cid].rpmsg_mutex);
	gcinfo[cid].rpdev = rpdev;
	mutex_unlock(&gcinfo[cid].rpmsg_mutex);
	ADSPRPC_INFO("opened rpmsg channel for %s\n",
		gcinfo[cid].subsys);
bail:
	if (err)
		ADSPRPC_ERR("rpmsg probe of %s cid %d failed\n",
			rpdev->dev.parent->of_node->name, cid);
	return err;
}

static void fastrpc_rpmsg_remove(struct rpmsg_device *rpdev)
{
	int err = 0;
	int cid = -1;

	VERIFY(err, !IS_ERR_OR_NULL(rpdev));
	if (err) {
		err = -ENODEV;
		return;
	}

	cid = get_cid_from_rpdev(rpdev);
	VERIFY(err, cid >= ADSP_DOMAIN_ID && cid < NUM_CHANNELS);
	if (err) {
		err = -ECHRNG;
		goto bail;
	}
	mutex_lock(&gcinfo[cid].rpmsg_mutex);
	gcinfo[cid].rpdev = NULL;
	mutex_unlock(&gcinfo[cid].rpmsg_mutex);
	ADSPRPC_INFO("closed rpmsg channel of %s\n",
		gcinfo[cid].subsys);
bail:
	if (err)
		ADSPRPC_ERR("rpmsg remove of %s cid %d failed\n",
			rpdev->dev.parent->of_node->name, cid);
}

static int fastrpc_rpmsg_callback(struct rpmsg_device *rpdev, void *data,
	int len, void *priv, u32 addr)
{
	struct smq_invoke_rsp *rsp = (struct smq_invoke_rsp *)data;
	struct smq_invoke_rspv2 *rspv2 = NULL;
	struct smq_invoke_ctx *ctx = NULL;
	struct fastrpc_apps *me = &gfa;
	uint32_t index, rsp_flags = 0, early_wake_time = 0, ver = 0;
	int err = 0, cid = -1, ignore_rpmsg_err = 0;
	struct fastrpc_channel_ctx *chan = NULL;
	unsigned long irq_flags = 0;
	int64_t ns = 0;

	cid = get_cid_from_rpdev(rpdev);
	VERIFY(err, (cid >= ADSP_DOMAIN_ID && cid <= NUM_CHANNELS));
	if (err) {
		err = -ECHRNG;
		goto bail;
	}

	chan = &me->channel[cid];
	VERIFY(err, (rsp && len >= sizeof(*rsp)));
	if (err)
		goto bail;

	if (len >= sizeof(struct smq_invoke_rspv2))
		rspv2 = (struct smq_invoke_rspv2 *)data;

	if (rspv2) {
		early_wake_time = rspv2->early_wake_time;
		rsp_flags = rspv2->flags;
		ver = rspv2->version;
	}
	trace_fastrpc_rpmsg_response(cid, rsp->ctx,
		rsp->retval, rsp_flags, early_wake_time);
	ns = get_timestamp_in_ns();
	fastrpc_update_rxmsg_buf(chan, rsp->ctx, rsp->retval,
		rsp_flags, early_wake_time, ver, ns);

	index = (uint32_t)GET_TABLE_IDX_FROM_CTXID(rsp->ctx);
	VERIFY(err, index < FASTRPC_CTX_MAX);
	if (err)
		goto bail;

	spin_lock_irqsave(&chan->ctxlock, irq_flags);
	ctx = chan->ctxtable[index];
	VERIFY(err, !IS_ERR_OR_NULL(ctx) &&
		(ctx->ctxid == (rsp->ctx & ~CONTEXT_PD_CHECK)) &&
		ctx->magic == FASTRPC_CTX_MAGIC);
	if (err) {
		/*
		 * Received an anticipatory COMPLETE_SIGNAL from DSP for a
		 * context after CPU successfully polling on memory and
		 * completed processing of context. Ignore the message.
		 */
		ignore_rpmsg_err = (rsp_flags == COMPLETE_SIGNAL) ? 1 : 0;
		goto bail_unlock;
	}

	if (rspv2) {
		VERIFY(err, rspv2->version == FASTRPC_RSP_VERSION2);
		if (err)
			goto bail_unlock;
	}
	context_notify_user(ctx, rsp->retval, rsp_flags, early_wake_time);
bail_unlock:
	spin_unlock_irqrestore(&chan->ctxlock, irq_flags);
bail:
	if (err) {
		err = -ENOKEY;
		if (!ignore_rpmsg_err)
			ADSPRPC_ERR(
				"invalid response data %pK, len %d from remote subsystem err %d\n",
				data, len, err);
		else
			me->duplicate_rsp_err_cnt++;
	}

	return err;
}

static int fastrpc_session_alloc(struct fastrpc_channel_ctx *chan, int secure,
					struct fastrpc_session_ctx **session)
{
	int err = 0;

	mutex_lock(&chan->smd_mutex);
	if (!*session)
		err = fastrpc_session_alloc_locked(chan, secure, session);
	mutex_unlock(&chan->smd_mutex);
	return err;
}

static void fastrpc_session_free(struct fastrpc_channel_ctx *chan,
				struct fastrpc_session_ctx *session)
{
	mutex_lock(&chan->smd_mutex);
	session->used = 0;
	mutex_unlock(&chan->smd_mutex);
}

static int fastrpc_file_free(struct fastrpc_file *fl)
{
	struct hlist_node *n = NULL;
	struct fastrpc_mmap *map = NULL, *lmap = NULL;
	unsigned long flags;
	int cid = -1, err = 0;

	if (!fl)
		return 0;
	cid = fl->cid;
	spin_lock(&fl->apps->hlock);
	fl->file_close = FASTRPC_PROCESS_EXIT_START;
	spin_unlock(&fl->apps->hlock);

	(void)fastrpc_release_current_dsp_process(fl);
	spin_lock(&fl->apps->hlock);
	if (!fl->is_ramdump_pend) {
		spin_unlock(&fl->apps->hlock);
		goto skip_dump_wait;
	}
	spin_unlock(&fl->apps->hlock);
	wait_for_completion(&fl->work);

skip_dump_wait:
	spin_lock(&fl->apps->hlock);
	hlist_del_init(&fl->hn);
	fl->is_ramdump_pend = false;
	fl->dsp_process_state = PROCESS_CREATE_DEFAULT;
	spin_unlock(&fl->apps->hlock);
	kfree(fl->debug_buf);
	kfree(fl->gidlist.gids);
	if (!fl->sctx) {
		kfree(fl->dev_pm_qos_req);
		kfree(fl);
		return 0;
	}

	//Dummy wake up to exit Async worker thread
	spin_lock_irqsave(&fl->aqlock, flags);
	atomic_add(1, &fl->async_queue_job_count);
	wake_up_interruptible(&fl->async_wait_queue);
	spin_unlock_irqrestore(&fl->aqlock, flags);
	if (!IS_ERR_OR_NULL(fl->init_mem)) {
		fastrpc_buf_free(fl->init_mem, 0);
		fl->init_mem = NULL;
	}
	fastrpc_context_list_dtor(fl);
	fastrpc_cached_buf_list_free(fl);
	if (!IS_ERR_OR_NULL(fl->hdr_bufs))
		kfree(fl->hdr_bufs);
	if (!IS_ERR_OR_NULL(fl->pers_hdr_buf))
		fastrpc_buf_free(fl->pers_hdr_buf, 0);
	mutex_lock(&fl->map_mutex);
	do {
		lmap = NULL;
		hlist_for_each_entry_safe(map, n, &fl->maps, hn) {
			hlist_del_init(&map->hn);
			lmap = map;
			break;
		}
		fastrpc_mmap_free(lmap, 1);
	} while (lmap);
	mutex_unlock(&fl->map_mutex);

	VERIFY(err, VALID_FASTRPC_CID(cid));
	if (fl->sctx)
		fastrpc_session_free(&fl->apps->channel[cid], fl->sctx);
	if (fl->secsctx)
		fastrpc_session_free(&fl->apps->channel[cid], fl->secsctx);

	fastrpc_remote_buf_list_free(fl);
	mutex_destroy(&fl->map_mutex);
	mutex_destroy(&fl->internal_map_mutex);
	kfree(fl->dev_pm_qos_req);
	kfree(fl);
	return 0;
}

static int fastrpc_device_release(struct inode *inode, struct file *file)
{
	struct fastrpc_file *fl = (struct fastrpc_file *)file->private_data;
	struct fastrpc_apps *me = &gfa;
	u32 ii;

	if (!fl)
		return 0;

	if (fl->qos_request && fl->dev_pm_qos_req) {
		for (ii = 0; ii < me->silvercores.corecount; ii++) {
			if (!dev_pm_qos_request_active(&fl->dev_pm_qos_req[ii]))
				continue;
			dev_pm_qos_remove_request(&fl->dev_pm_qos_req[ii]);
		}
	}
	debugfs_remove(fl->debugfs_file);
	fastrpc_file_free(fl);
	file->private_data = NULL;

	return 0;
}

static ssize_t fastrpc_debugfs_read(struct file *filp, char __user *buffer,
					 size_t count, loff_t *position)
{
	struct fastrpc_apps *me = &gfa;
	struct fastrpc_file *fl = filp->private_data;
	struct hlist_node *n;
	struct fastrpc_buf *buf = NULL;
	struct fastrpc_mmap *map = NULL;
	struct fastrpc_mmap *gmaps = NULL;
	struct smq_invoke_ctx *ictx = NULL;
	struct fastrpc_channel_ctx *chan = NULL;
	unsigned int len = 0;
	int i, j, sess_used = 0, ret = 0;
	char *fileinfo = NULL;
	char single_line[] = "----------------";
	char title[] = "=========================";

	fileinfo = kzalloc(DEBUGFS_SIZE, GFP_KERNEL);
	if (!fileinfo) {
		ret = -ENOMEM;
		goto bail;
	}
	if (fl == NULL) {
		len += scnprintf(fileinfo + len, DEBUGFS_SIZE - len,
			"\n%s %s %s\n", title, " CHANNEL INFO ", title);
		len += scnprintf(fileinfo + len, DEBUGFS_SIZE - len,
			"%-7s|%-10s|%-14s|%-9s|%-13s\n",
			"subsys", "sesscount", "issubsystemup",
			"ssrcount", "session_used");
		len += scnprintf(fileinfo + len, DEBUGFS_SIZE - len,
			"-%s%s%s%s-\n", single_line, single_line,
			single_line, single_line);
		for (i = 0; i < NUM_CHANNELS; i++) {
			sess_used = 0;
			chan = &gcinfo[i];
			len += scnprintf(fileinfo + len,
				DEBUGFS_SIZE - len, "%-7s", chan->subsys);
			len += scnprintf(fileinfo + len,
				DEBUGFS_SIZE - len, "|%-10u",
				chan->sesscount);
			len += scnprintf(fileinfo + len,
				DEBUGFS_SIZE - len, "|%-14d",
				chan->issubsystemup);
			len += scnprintf(fileinfo + len,
				DEBUGFS_SIZE - len, "|%-9u",
				chan->ssrcount);
			for (j = 0; j < chan->sesscount; j++)
				sess_used += chan->session[j].used;
			len += scnprintf(fileinfo + len,
				DEBUGFS_SIZE - len, "|%-13d\n", sess_used);
		}
		len += scnprintf(fileinfo + len, DEBUGFS_SIZE - len,
			"\n%s%s%s\n", "=============",
			" CMA HEAP ", "==============");
		len += scnprintf(fileinfo + len,
			DEBUGFS_SIZE - len, "%-20s|%-20s\n", "addr", "size");
		len += scnprintf(fileinfo + len,
			DEBUGFS_SIZE - len, "--%s%s---\n",
			single_line, single_line);
		len += scnprintf(fileinfo + len, DEBUGFS_SIZE - len,
			"\n==========%s %s %s===========\n",
			title, " GMAPS ", title);
		len += scnprintf(fileinfo + len, DEBUGFS_SIZE - len,
			"%-20s|%-20s|%-20s|%-20s\n",
			"fd", "phys", "size", "va");
		len += scnprintf(fileinfo + len, DEBUGFS_SIZE - len,
			"%s%s%s%s%s\n", single_line, single_line,
			single_line, single_line, single_line);
		spin_lock(&me->hlock);
		hlist_for_each_entry_safe(gmaps, n, &me->maps, hn) {
			len += scnprintf(fileinfo + len, DEBUGFS_SIZE - len,
				"%-20d|0x%-18llX|0x%-18X|0x%-20lX\n\n",
				gmaps->fd, gmaps->phys,
				(uint32_t)gmaps->size,
				gmaps->va);
		}
		spin_unlock(&me->hlock);
		len += scnprintf(fileinfo + len, DEBUGFS_SIZE - len,
			"%-20s|%-20s|%-20s|%-20s\n",
			"len", "refs", "raddr", "flags");
		len += scnprintf(fileinfo + len, DEBUGFS_SIZE - len,
			"%s%s%s%s%s\n", single_line, single_line,
			single_line, single_line, single_line);
		spin_lock(&me->hlock);
		hlist_for_each_entry_safe(gmaps, n, &me->maps, hn) {
			len += scnprintf(fileinfo + len, DEBUGFS_SIZE - len,
				"0x%-18X|%-20d|%-20lu|%-20u\n",
				(uint32_t)gmaps->len, gmaps->refs,
				gmaps->raddr, gmaps->flags);
		}
		spin_unlock(&me->hlock);
	} else {
		len += scnprintf(fileinfo + len, DEBUGFS_SIZE - len,
			"\n%s %13s %d\n", "cid", ":", fl->cid);
		len += scnprintf(fileinfo + len, DEBUGFS_SIZE - len,
			"%s %12s %d\n", "tgid", ":", fl->tgid);
		len += scnprintf(fileinfo + len, DEBUGFS_SIZE - len,
			"%s %7s %d\n", "sessionid", ":", fl->sessionid);
		len += scnprintf(fileinfo + len, DEBUGFS_SIZE - len,
			"%s %8s %u\n", "ssrcount", ":", fl->ssrcount);
		len += scnprintf(fileinfo + len, DEBUGFS_SIZE - len,
			"%s %14s %d\n", "pd", ":", fl->pd);
		len += scnprintf(fileinfo + len, DEBUGFS_SIZE - len,
			"%s %9s %s\n", "servloc_name", ":", fl->servloc_name);
		len += scnprintf(fileinfo + len, DEBUGFS_SIZE - len,
			"%s %6s %d\n", "file_close", ":", fl->file_close);
		len += scnprintf(fileinfo + len, DEBUGFS_SIZE - len,
			"%s %9s %d\n", "profile", ":", fl->profile);
		len += scnprintf(fileinfo + len, DEBUGFS_SIZE - len,
			"%s %3s %d\n", "smmu.coherent", ":",
			fl->sctx->smmu.coherent);
		len += scnprintf(fileinfo + len, DEBUGFS_SIZE - len,
			"%s %4s %d\n", "smmu.enabled", ":",
			fl->sctx->smmu.enabled);
		len += scnprintf(fileinfo + len, DEBUGFS_SIZE - len,
			"%s %9s %d\n", "smmu.cb", ":", fl->sctx->smmu.cb);
		len += scnprintf(fileinfo + len, DEBUGFS_SIZE - len,
			"%s %5s %d\n", "smmu.secure", ":",
			fl->sctx->smmu.secure);
		len += scnprintf(fileinfo + len, DEBUGFS_SIZE - len,
			"%s %5s %d\n", "smmu.faults", ":",
			fl->sctx->smmu.faults);

		len += scnprintf(fileinfo + len, DEBUGFS_SIZE - len,
			"\n=======%s %s %s======\n", title,
			" LIST OF MAPS ", title);
		len += scnprintf(fileinfo + len, DEBUGFS_SIZE - len,
			"%-20s|%-20s|%-20s\n", "va", "phys", "size");
		len += scnprintf(fileinfo + len, DEBUGFS_SIZE - len,
			"%s%s%s%s%s\n",
			single_line, single_line, single_line,
			single_line, single_line);
		mutex_lock(&fl->map_mutex);
		hlist_for_each_entry_safe(map, n, &fl->maps, hn) {
			len += scnprintf(fileinfo + len, DEBUGFS_SIZE - len,
				"0x%-20lX|0x%-20llX|0x%-20zu\n\n",
				map->va, map->phys,
				map->size);
		}
		mutex_unlock(&fl->map_mutex);
		len += scnprintf(fileinfo + len, DEBUGFS_SIZE - len,
			"%-20s|%-20s|%-20s|%-20s\n",
			"len", "refs",
			"raddr", "uncached");
		len += scnprintf(fileinfo + len, DEBUGFS_SIZE - len,
			"%s%s%s%s%s\n",
			single_line, single_line, single_line,
			single_line, single_line);
		mutex_lock(&fl->map_mutex);
		hlist_for_each_entry_safe(map, n, &fl->maps, hn) {
			len += scnprintf(fileinfo + len, DEBUGFS_SIZE - len,
				"%-20zu|%-20d|0x%-20lX|%-20d\n\n",
				map->len, map->refs, map->raddr,
				map->uncached);
		}
		mutex_unlock(&fl->map_mutex);
		len += scnprintf(fileinfo + len, DEBUGFS_SIZE - len,
			"%-20s|%-20s\n", "secure", "attr");
		len += scnprintf(fileinfo + len, DEBUGFS_SIZE - len,
			"%s%s%s%s%s\n",
			single_line, single_line, single_line,
			single_line, single_line);
		mutex_lock(&fl->map_mutex);
		hlist_for_each_entry_safe(map, n, &fl->maps, hn) {
			len += scnprintf(fileinfo + len, DEBUGFS_SIZE - len,
				"%-20d|0x%-20lX\n\n",
				map->secure, map->attr);
		}
		mutex_unlock(&fl->map_mutex);
		len += scnprintf(fileinfo + len, DEBUGFS_SIZE - len,
			"\n======%s %s %s======\n", title,
			" LIST OF BUFS ", title);
		spin_lock(&fl->hlock);
		len += scnprintf(fileinfo + len, DEBUGFS_SIZE - len,
			"%-19s|%-19s|%-19s\n",
			"virt", "phys", "size");
		len += scnprintf(fileinfo + len, DEBUGFS_SIZE - len,
			"%s%s%s%s%s\n", single_line, single_line,
			single_line, single_line, single_line);
		hlist_for_each_entry_safe(buf, n, &fl->cached_bufs, hn) {
			len += scnprintf(fileinfo + len,
				DEBUGFS_SIZE - len,
				"0x%-17p|0x%-17llX|%-19zu\n",
				buf->virt, (uint64_t)buf->phys, buf->size);
		}

		len += scnprintf(fileinfo + len, DEBUGFS_SIZE - len,
			"\n%s %s %s\n", title,
			" LIST OF PENDING SMQCONTEXTS ", title);
		len += scnprintf(fileinfo + len, DEBUGFS_SIZE - len,
			"%-20s|%-10s|%-10s|%-10s|%-20s\n",
			"sc", "pid", "tgid", "used", "ctxid");
		len += scnprintf(fileinfo + len, DEBUGFS_SIZE - len,
			"%s%s%s%s%s\n", single_line, single_line,
			single_line, single_line, single_line);
		hlist_for_each_entry_safe(ictx, n, &fl->clst.pending, hn) {
			len += scnprintf(fileinfo + len, DEBUGFS_SIZE - len,
				"0x%-18X|%-10d|%-10d|%-10zu|0x%-20llX\n\n",
				ictx->sc, ictx->pid, ictx->tgid,
				ictx->used, ictx->ctxid);
		}

		len += scnprintf(fileinfo + len, DEBUGFS_SIZE - len,
			"\n%s %s %s\n", title,
			" LIST OF INTERRUPTED SMQCONTEXTS ", title);
		len += scnprintf(fileinfo + len, DEBUGFS_SIZE - len,
			"%-20s|%-10s|%-10s|%-10s|%-20s\n",
			"sc", "pid", "tgid", "used", "ctxid");
		len += scnprintf(fileinfo + len, DEBUGFS_SIZE - len,
			"%s%s%s%s%s\n", single_line, single_line,
			single_line, single_line, single_line);
		hlist_for_each_entry_safe(ictx, n, &fl->clst.interrupted, hn) {
			len += scnprintf(fileinfo + len, DEBUGFS_SIZE - len,
			"%-20u|%-20d|%-20d|%-20zu|0x%-20llX\n\n",
			ictx->sc, ictx->pid, ictx->tgid,
			ictx->used, ictx->ctxid);
		}
		spin_unlock(&fl->hlock);
	}
	if (len > DEBUGFS_SIZE)
		len = DEBUGFS_SIZE;
	ret = simple_read_from_buffer(buffer, count, position, fileinfo, len);
	kfree(fileinfo);
bail:
	return ret;
}

static const struct file_operations debugfs_fops = {
	.open = simple_open,
	.read = fastrpc_debugfs_read,
};

static int fastrpc_channel_open(struct fastrpc_file *fl)
{
	struct fastrpc_apps *me = &gfa;
	int cid = -1, err = 0;

	VERIFY(err, fl && fl->sctx && fl->cid >= 0 && fl->cid < NUM_CHANNELS);
	if (err) {
		ADSPRPC_ERR("kernel session not initialized yet for %s\n",
			current->comm);
		err = -EBADR;
		return err;
	}
	cid = fl->cid;

	mutex_lock(&me->channel[cid].rpmsg_mutex);
	VERIFY(err, NULL != me->channel[cid].rpdev);
	if (err) {
		err = -ENODEV;
		mutex_unlock(&me->channel[cid].rpmsg_mutex);
		goto bail;
	}
	mutex_unlock(&me->channel[cid].rpmsg_mutex);

	mutex_lock(&me->channel[cid].smd_mutex);
	if (me->channel[cid].ssrcount !=
				 me->channel[cid].prevssrcount) {
		if (!me->channel[cid].issubsystemup) {
			err = -ECONNREFUSED;
			mutex_unlock(&me->channel[cid].smd_mutex);
			goto bail;
		}
	}
	fl->ssrcount = me->channel[cid].ssrcount;

	if (cid == ADSP_DOMAIN_ID && me->channel[cid].ssrcount !=
			 me->channel[cid].prevssrcount) {
		mutex_lock(&fl->map_mutex);
		err = fastrpc_mmap_remove_ssr(fl);
		if (err)
			ADSPRPC_WARN(
				"failed to unmap remote heap for %s (err %d)\n",
				me->channel[cid].subsys, err);
		mutex_unlock(&fl->map_mutex);
		me->channel[cid].prevssrcount =
					me->channel[cid].ssrcount;
	}
	mutex_unlock(&me->channel[cid].smd_mutex);

bail:
	return err;
}

static inline void fastrpc_register_wakeup_source(struct device *dev,
	const char *client_name, struct wakeup_source **device_wake_source)
{
	struct wakeup_source *wake_source = NULL;

	wake_source = wakeup_source_register(dev, client_name);
	if (IS_ERR_OR_NULL(wake_source)) {
		ADSPRPC_ERR(
			"wakeup_source_register failed for dev %s, client %s with err %ld\n",
			dev_name(dev), client_name, PTR_ERR(wake_source));
		return;
	}
	*device_wake_source = wake_source;
}

static int fastrpc_device_open(struct inode *inode, struct file *filp)
{
	int err = 0;
	struct fastrpc_file *fl = NULL;
	struct fastrpc_apps *me = &gfa;

	/*
	 * Indicates the device node opened
	 * MINOR_NUM_DEV or MINOR_NUM_SECURE_DEV
	 */
	int dev_minor = MINOR(inode->i_rdev);

	VERIFY(err, ((dev_minor == MINOR_NUM_DEV) ||
			(dev_minor == MINOR_NUM_SECURE_DEV)));
	if (err) {
		ADSPRPC_ERR("Invalid dev minor num %d\n",
			dev_minor);
		return err;
	}

	VERIFY(err, NULL != (fl = kzalloc(sizeof(*fl), GFP_KERNEL)));
	if (err) {
		err = -ENOMEM;
		return err;
	}

	context_list_ctor(&fl->clst);
	spin_lock_init(&fl->hlock);
	spin_lock_init(&fl->aqlock);
	INIT_HLIST_HEAD(&fl->maps);
	INIT_HLIST_HEAD(&fl->cached_bufs);
	fl->num_cached_buf = 0;
	INIT_HLIST_HEAD(&fl->remote_bufs);
	init_waitqueue_head(&fl->async_wait_queue);
	INIT_HLIST_NODE(&fl->hn);
	fl->sessionid = 0;
	fl->tgid_open = current->tgid;
	fl->apps = me;
	fl->mode = FASTRPC_MODE_SERIAL;
	fl->cid = -1;
	fl->dev_minor = dev_minor;
	fl->init_mem = NULL;
	fl->qos_request = 0;
	fl->dsp_proc_init = 0;
	fl->is_ramdump_pend = false;
	fl->dsp_process_state = PROCESS_CREATE_DEFAULT;
	init_completion(&fl->work);
	fl->file_close = FASTRPC_PROCESS_DEFAULT_STATE;
	filp->private_data = fl;
	mutex_init(&fl->internal_map_mutex);
	mutex_init(&fl->map_mutex);
	spin_lock(&me->hlock);
	hlist_add_head(&fl->hn, &me->drivers);
	spin_unlock(&me->hlock);
	fl->dev_pm_qos_req = kcalloc(me->silvercores.corecount,
				sizeof(struct dev_pm_qos_request),
				GFP_KERNEL);

	return 0;
}

static int fastrpc_get_process_gids(struct gid_list *gidlist)
{
	struct group_info *group_info = get_current_groups();
	int i = 0, err = 0, num_gids = group_info->ngroups + 1;
	unsigned int *gids = NULL;

	gids = kcalloc(num_gids, sizeof(unsigned int), GFP_KERNEL);
	if (!gids) {
		err = -ENOMEM;
		goto bail;
	}

	/* Get the real GID */
	gids[0] = __kgid_val(current_gid());

	/* Get the supplemental GIDs */
	for (i = 1; i < num_gids; i++)
		gids[i] = __kgid_val(group_info->gid[i - 1]);

	sort(gids, num_gids, sizeof(*gids), uint_cmp_func, NULL);
	gidlist->gids = gids;
	gidlist->gidcount = num_gids;
bail:
	if (err)
		kfree(gids);
	return err;
}

static int fastrpc_set_process_info(struct fastrpc_file *fl)
{
	int err = 0, buf_size = 0;
	char strpid[PID_SIZE];
	char cur_comm[TASK_COMM_LEN];

	memcpy(cur_comm, current->comm, TASK_COMM_LEN);
	cur_comm[TASK_COMM_LEN-1] = '\0';
	fl->tgid = current->tgid;

	/*
	 * Third-party apps don't have permission to open the fastrpc device, so
	 * it is opened on their behalf by DSP HAL. This is detected by
	 * comparing current PID with the one stored during device open.
	 */
	if (current->tgid != fl->tgid_open)
		fl->untrusted_process = true;
	snprintf(strpid, PID_SIZE, "%d", current->pid);
	if (debugfs_root) {
		buf_size = strlen(cur_comm) + strlen("_")
			+ strlen(strpid) + 1;

		spin_lock(&fl->hlock);
		if (fl->debug_buf_alloced_attempted) {
			spin_unlock(&fl->hlock);
			return err;
		}
		fl->debug_buf_alloced_attempted = 1;
		spin_unlock(&fl->hlock);
		fl->debug_buf = kzalloc(buf_size, GFP_KERNEL);

		if (!fl->debug_buf) {
			err = -ENOMEM;
			return err;
		}
		snprintf(fl->debug_buf, buf_size, "%.10s%s%d",
			cur_comm, "_", current->pid);
		fl->debugfs_file = debugfs_create_file(fl->debug_buf, 0644,
			debugfs_root, fl, &debugfs_fops);
		if (IS_ERR_OR_NULL(fl->debugfs_file)) {
			pr_warn("Error: %s: %s: failed to create debugfs file %s\n",
				cur_comm, __func__, fl->debug_buf);
			fl->debugfs_file = NULL;
			kfree(fl->debug_buf);
			fl->debug_buf = NULL;
		}
	}
	return err;
}

static int fastrpc_get_info(struct fastrpc_file *fl, uint32_t *info)
{
	int err = 0;
	uint32_t cid;
	struct fastrpc_apps *me = &gfa;

	VERIFY(err, fl != NULL);
	if (err)
		goto bail;
	fastrpc_get_process_gids(&fl->gidlist);
	err = fastrpc_set_process_info(fl);
	if (err)
		goto bail;
	cid = *info;
	if (fl->cid == -1) {
		struct fastrpc_channel_ctx *chan = &me->channel[cid];
		VERIFY(err, cid < NUM_CHANNELS);
		if (err) {
			err = -ECHRNG;
			goto bail;
		}
		/* Check to see if the device node is non-secure */
		if (fl->dev_minor == MINOR_NUM_DEV) {
			/*
			 * If an app is trying to offload to a secure remote
			 * channel by opening the non-secure device node, allow
			 * the access if the subsystem supports unsigned
			 * offload. Untrusted apps will be restricted from
			 * offloading to signed PD using DSP HAL.
			 */
			if (chan->secure == SECURE_CHANNEL
			&& !chan->unsigned_support) {
				ADSPRPC_ERR(
				"cannot use domain %d with non-secure device\n",
				cid);
				err = -EACCES;
				goto bail;
			}
		}
		fl->cid = cid;
		fl->ssrcount = fl->apps->channel[cid].ssrcount;
		mutex_lock(&fl->apps->channel[cid].smd_mutex);
		err = fastrpc_session_alloc_locked(&fl->apps->channel[cid],
				0, &fl->sctx);
		mutex_unlock(&fl->apps->channel[cid].smd_mutex);
		if (err)
			goto bail;
	}
	VERIFY(err, fl->sctx != NULL);
	if (err)
		goto bail;
	*info = (fl->sctx->smmu.enabled ? 1 : 0);
bail:
	return err;
}

static int fastrpc_internal_control(struct fastrpc_file *fl,
					struct fastrpc_ioctl_control *cp)
{
	int err = 0;
	unsigned int latency;
	struct fastrpc_apps *me = &gfa;
	u32 silver_core_count = me->silvercores.corecount, ii = 0, cpu;

	VERIFY(err, !IS_ERR_OR_NULL(fl) && !IS_ERR_OR_NULL(fl->apps));
	if (err) {
		err = -EBADF;
		goto bail;
	}
	VERIFY(err, !IS_ERR_OR_NULL(cp));
	if (err) {
		err = -EINVAL;
		goto bail;
	}

	switch (cp->req) {
	case FASTRPC_CONTROL_LATENCY:
		latency = cp->lp.enable == FASTRPC_LATENCY_CTRL_ENB ?
			fl->apps->latency : PM_QOS_DEFAULT_VALUE;
		VERIFY(err, latency != 0);
		if (err) {
			err = -EINVAL;
			goto bail;
		}

		VERIFY(err, me->silvercores.coreno && fl->dev_pm_qos_req);
		if (err)
			goto bail;

		for (ii = 0; ii < silver_core_count; ii++) {
			cpu = me->silvercores.coreno[ii];
			if (!fl->qos_request) {
				err = dev_pm_qos_add_request(
						get_cpu_device(cpu),
						&fl->dev_pm_qos_req[ii],
						DEV_PM_QOS_RESUME_LATENCY,
						latency);
			} else {
				err = dev_pm_qos_update_request(
						&fl->dev_pm_qos_req[ii],
						latency);
			}
			if (err < 0) {
				pr_warn("adsprpc: %s: %s: PM voting for cpu:%d failed, err %d, QoS update %d\n",
					current->comm, __func__, cpu,
					err, fl->qos_request);
				break;
			}
		}
		if (err >= 0)
			fl->qos_request = 1;

		/* Ensure CPU feature map updated to DSP for early WakeUp */
		fastrpc_send_cpuinfo_to_dsp(fl);
		break;
	case FASTRPC_CONTROL_KALLOC:
		cp->kalloc.kalloc_support = 1;
		break;
	case FASTRPC_CONTROL_WAKELOCK:
		if (fl->dev_minor != MINOR_NUM_SECURE_DEV) {
			ADSPRPC_ERR(
				"PM voting not allowed for non-secure device node %d\n",
				fl->dev_minor);
			err = -EPERM;
			goto bail;
		}
		fl->wake_enable = cp->wp.enable;
		break;
	case FASTRPC_CONTROL_PM:
		if (!fl->wake_enable) {
			/* Kernel PM voting not requested by this application */
			err = -EACCES;
			goto bail;
		}
		if (cp->pm.timeout > MAX_PM_TIMEOUT_MS)
			fl->ws_timeout = MAX_PM_TIMEOUT_MS;
		else
			fl->ws_timeout = cp->pm.timeout;
		fastrpc_pm_awake(fl, gcinfo[fl->cid].secure);
		break;
	case FASTRPC_CONTROL_DSPPROCESS_CLEAN:
		(void)fastrpc_release_current_dsp_process(fl);
		break;
	default:
		err = -EBADRQC;
		break;
	}
bail:
	return err;
}

static int fastrpc_check_pd_status(struct fastrpc_file *fl, char *sloc_name)
{
	int err = 0, session = -1, cid = -1;
	struct fastrpc_apps *me = &gfa;

	if (fl->servloc_name && sloc_name
		&& !strcmp(fl->servloc_name, sloc_name)) {
		err = fastrpc_get_spd_session(sloc_name, &session, &cid);
		if (err || cid != fl->cid)
			goto bail;
		if (!me->channel[cid].spd[session].ispdup) {
			err = -ENOTCONN;
			goto bail;
		}
	}
bail:
	return err;
}

static int fastrpc_setmode(unsigned long ioctl_param,
				struct fastrpc_file *fl)
{
	int err = 0;

	switch ((uint32_t)ioctl_param) {
	case FASTRPC_MODE_PARALLEL:
	case FASTRPC_MODE_SERIAL:
		fl->mode = (uint32_t)ioctl_param;
		break;
	case FASTRPC_MODE_PROFILE:
		fl->profile = (uint32_t)ioctl_param;
		break;
	case FASTRPC_MODE_SESSION:
		if (fl->untrusted_process) {
			err = -EPERM;
			ADSPRPC_ERR(
				"multiple sessions not allowed for untrusted apps\n");
			goto bail;
		}
		fl->sessionid = 1;
		fl->tgid |= (1 << SESSION_ID_INDEX);
		break;
	default:
		err = -ENOTTY;
		break;
	}
bail:
	return err;
}

static int fastrpc_control(struct fastrpc_ioctl_control *cp,
				void *param, struct fastrpc_file *fl)
{
	int err = 0;

	K_COPY_FROM_USER(err, 0, cp, param,
			sizeof(*cp));
	if (err) {
		err = -EFAULT;
		goto bail;
	}
	VERIFY(err, 0 == (err = fastrpc_internal_control(fl, cp)));
	if (err)
		goto bail;
	if (cp->req == FASTRPC_CONTROL_KALLOC) {
		K_COPY_TO_USER(err, 0, param, cp, sizeof(*cp));
		if (err) {
			err = -EFAULT;
			goto bail;
		}
	}
bail:
	return err;
}

static int fastrpc_get_dsp_info(
		struct fastrpc_ioctl_capability *cap,
		void *param, struct fastrpc_file *fl)
{
	int err = 0;

	K_COPY_FROM_USER(err, 0, cap, param,
			sizeof(struct fastrpc_ioctl_capability));
	VERIFY(err, cap->domain < NUM_CHANNELS);
	if (err) {
		err = -ECHRNG;
		goto bail;
	}
	cap->capability = 0;

	err = fastrpc_get_info_from_kernel(cap, fl);
	if (err)
		goto bail;
	K_COPY_TO_USER(err, 0, &((struct fastrpc_ioctl_capability *)
		param)->capability, &cap->capability, sizeof(cap->capability));
bail:
	return err;
}

union fastrpc_ioctl_param {
	struct fastrpc_ioctl_invoke_async inv;
	struct fastrpc_ioctl_mem_map mem_map;
	struct fastrpc_ioctl_mem_unmap mem_unmap;
	struct fastrpc_ioctl_mmap mmap;
	struct fastrpc_ioctl_mmap_64 mmap64;
	struct fastrpc_ioctl_munmap munmap;
	struct fastrpc_ioctl_munmap_64 munmap64;
	struct fastrpc_ioctl_munmap_fd munmap_fd;
	struct fastrpc_ioctl_init_attrs init;
	struct fastrpc_ioctl_control cp;
	struct fastrpc_ioctl_capability cap;
	struct fastrpc_ioctl_invoke2 inv2;
};

static inline int fastrpc_mmap_device_ioctl(struct fastrpc_file *fl,
		unsigned int ioctl_num,	union fastrpc_ioctl_param *p,
		void *param)
{
	union {
		struct fastrpc_ioctl_mmap mmap;
		struct fastrpc_ioctl_munmap munmap;
	} i;
	int err = 0;

	switch (ioctl_num) {
	case FASTRPC_IOCTL_MEM_MAP:
		K_COPY_FROM_USER(err, 0, &p->mem_map, param,
						sizeof(p->mem_map));
		if (err) {
			err = -EFAULT;
			goto bail;
		}
		VERIFY(err, 0 == (err = fastrpc_internal_mem_map(fl,
						&p->mem_map)));
		if (err)
			goto bail;
		K_COPY_TO_USER(err, 0, param, &p->mem_map, sizeof(p->mem_map));
		if (err) {
			err = -EFAULT;
			goto bail;
		}
		break;
	case FASTRPC_IOCTL_MEM_UNMAP:
		K_COPY_FROM_USER(err, 0, &p->mem_unmap, param,
						sizeof(p->mem_unmap));
		if (err) {
			err = -EFAULT;
			goto bail;
		}
		VERIFY(err, 0 == (err = fastrpc_internal_mem_unmap(fl,
						&p->mem_unmap)));
		if (err)
			goto bail;
		K_COPY_TO_USER(err, 0, param, &p->mem_unmap,
					sizeof(p->mem_unmap));
		if (err) {
			err = -EFAULT;
			goto bail;
		}
		break;
	case FASTRPC_IOCTL_MMAP:
		K_COPY_FROM_USER(err, 0, &p->mmap, param,
						sizeof(p->mmap));
		if (err) {
			err = -EFAULT;
			goto bail;
		}
		VERIFY(err, 0 == (err = fastrpc_internal_mmap(fl, &p->mmap)));
		if (err)
			goto bail;
		K_COPY_TO_USER(err, 0, param, &p->mmap, sizeof(p->mmap));
		if (err) {
			err = -EFAULT;
			goto bail;
		}
		break;
	case FASTRPC_IOCTL_MUNMAP:
		K_COPY_FROM_USER(err, 0, &p->munmap, param,
						sizeof(p->munmap));
		if (err) {
			err = -EFAULT;
			goto bail;
		}
		VERIFY(err, 0 == (err = fastrpc_internal_munmap(fl,
							&p->munmap)));
		if (err)
			goto bail;
		break;
	case FASTRPC_IOCTL_MMAP_64:
		K_COPY_FROM_USER(err, 0, &p->mmap64, param,
						sizeof(p->mmap64));
		if (err) {
			err = -EFAULT;
			goto bail;
		}
		get_fastrpc_ioctl_mmap_64(&p->mmap64, &i.mmap);
		VERIFY(err, 0 == (err = fastrpc_internal_mmap(fl, &i.mmap)));
		if (err)
			goto bail;
		put_fastrpc_ioctl_mmap_64(&p->mmap64, &i.mmap);
		K_COPY_TO_USER(err, 0, param, &p->mmap64, sizeof(p->mmap64));
		if (err) {
			err = -EFAULT;
			goto bail;
		}
		break;
	case FASTRPC_IOCTL_MUNMAP_64:
		K_COPY_FROM_USER(err, 0, &p->munmap64, param,
						sizeof(p->munmap64));
		if (err) {
			err = -EFAULT;
			goto bail;
		}
		get_fastrpc_ioctl_munmap_64(&p->munmap64, &i.munmap);
		VERIFY(err, 0 == (err = fastrpc_internal_munmap(fl,
							&i.munmap)));
		if (err)
			goto bail;
		break;
	case FASTRPC_IOCTL_MUNMAP_FD:
		K_COPY_FROM_USER(err, 0, &p->munmap_fd, param,
			sizeof(p->munmap_fd));
		if (err) {
			err = -EFAULT;
			goto bail;
		}
		VERIFY(err, 0 == (err = fastrpc_internal_munmap_fd(fl,
			&p->munmap_fd)));
		if (err)
			goto bail;
		break;
	default:
		err = -ENOTTY;
		pr_info("bad ioctl: %d\n", ioctl_num);
		break;
	}
bail:
	return err;
}

static int fastrpc_update_cdsp_support(struct fastrpc_file *fl)
{
	struct fastrpc_ioctl_capability *dsp_query;
	struct fastrpc_apps *me = &gfa;
	int err = 0;

	VERIFY(err, NULL != (dsp_query = kzalloc(sizeof(*dsp_query),
				GFP_KERNEL)));
	if (err)
		goto bail;
	dsp_query->domain = CDSP_DOMAIN_ID;
	dsp_query->attribute_ID = UNSIGNED_PD_SUPPORT;
	err = fastrpc_get_info_from_kernel(dsp_query, fl);
	if (err)
		goto bail;
	if (!dsp_query->capability)
		me->channel[CDSP_DOMAIN_ID].unsigned_support = false;
bail:
	kfree(dsp_query);
	return err;
}

static long fastrpc_device_ioctl(struct file *file, unsigned int ioctl_num,
				 unsigned long ioctl_param)
{
	union fastrpc_ioctl_param p;
	void *param = (char *)ioctl_param;
	struct fastrpc_file *fl = (struct fastrpc_file *)file->private_data;
	int size = 0, err = 0;
	uint32_t info;
	static bool isquerydone;

	p.inv.fds = NULL;
	p.inv.attrs = NULL;
	p.inv.crc = NULL;
	p.inv.perf_kernel = NULL;
	p.inv.perf_dsp = NULL;
	p.inv.job = NULL;

	err = fastrpc_check_pd_status(fl,
			AUDIO_PDR_SERVICE_LOCATION_CLIENT_NAME);
	if (err)
		goto bail;

	spin_lock(&fl->hlock);
	if (fl->file_close >= FASTRPC_PROCESS_EXIT_START) {
		err = -ESHUTDOWN;
		pr_warn("adsprpc: fastrpc_device_release is happening, So not sending any new requests to DSP\n");
		spin_unlock(&fl->hlock);
		goto bail;
	}
	spin_unlock(&fl->hlock);

	switch (ioctl_num) {
	case FASTRPC_IOCTL_INVOKE:
		size = sizeof(struct fastrpc_ioctl_invoke);
		/* fall through */
	case FASTRPC_IOCTL_INVOKE_FD:
		if (!size)
			size = sizeof(struct fastrpc_ioctl_invoke_fd);
		/* fall through */
	case FASTRPC_IOCTL_INVOKE_ATTRS:
		if (!size)
			size = sizeof(struct fastrpc_ioctl_invoke_attrs);
		/* fall through */
	case FASTRPC_IOCTL_INVOKE_CRC:
		if (!size)
			size = sizeof(struct fastrpc_ioctl_invoke_crc);
	case FASTRPC_IOCTL_INVOKE_PERF:
		if (!size)
			size = sizeof(struct fastrpc_ioctl_invoke_perf);
		K_COPY_FROM_USER(err, 0, &p.inv, param, size);
		if (err) {
			err = -EFAULT;
			goto bail;
		}
		VERIFY(err, 0 == (err = fastrpc_internal_invoke(fl, fl->mode,
						USER_MSG, &p.inv)));
		if (err)
			goto bail;
		break;
	case FASTRPC_IOCTL_INVOKE2:
		K_COPY_FROM_USER(err, 0, &p.inv2, param,
					sizeof(struct fastrpc_ioctl_invoke2));
		if (err) {
			err = -EFAULT;
			goto bail;
		}
		VERIFY(err, 0 == (err = fastrpc_internal_invoke2(fl, &p.inv2)));
		if (err)
			goto bail;
		break;
	case FASTRPC_IOCTL_SETMODE:
		err = fastrpc_setmode(ioctl_param, fl);
		break;
	case FASTRPC_IOCTL_CONTROL:
		err = fastrpc_control(&p.cp, param, fl);
		break;
	case FASTRPC_IOCTL_GETINFO:
	    K_COPY_FROM_USER(err, 0, &info, param, sizeof(info));
		if (err) {
			err = -EFAULT;
			goto bail;
		}
		VERIFY(err, 0 == (err = fastrpc_get_info(fl, &info)));
		if (err)
			goto bail;
		K_COPY_TO_USER(err, 0, param, &info, sizeof(info));
		if (err) {
			err = -EFAULT;
			goto bail;
		}
		break;
	case FASTRPC_IOCTL_INIT:
		p.init.attrs = 0;
		p.init.siglen = 0;
		size = sizeof(struct fastrpc_ioctl_init);
		/* fall through */
	case FASTRPC_IOCTL_INIT_ATTRS:
		if (!size)
			size = sizeof(struct fastrpc_ioctl_init_attrs);
		K_COPY_FROM_USER(err, 0, &p.init, param, size);
		if (err) {
			err = -EFAULT;
			goto bail;
		}
		VERIFY(err, 0 == (err = fastrpc_init_process(fl, &p.init)));
		if (err)
			goto bail;
		if ((fl->cid == CDSP_DOMAIN_ID) && !isquerydone) {
			if (!fastrpc_update_cdsp_support(fl))
				isquerydone = true;
		}
		break;
	case FASTRPC_IOCTL_GET_DSP_INFO:
		err = fastrpc_get_dsp_info(&p.cap, param, fl);
		break;
	case FASTRPC_IOCTL_MEM_MAP:
		/* fall through */
	case FASTRPC_IOCTL_MEM_UNMAP:
		/* fall through */
	case FASTRPC_IOCTL_MMAP:
		/* fall through */
	case FASTRPC_IOCTL_MUNMAP:
		/* fall through */
	case FASTRPC_IOCTL_MMAP_64:
		/* fall through */
	case FASTRPC_IOCTL_MUNMAP_64:
		/* fall through */
	case FASTRPC_IOCTL_MUNMAP_FD:
		err = fastrpc_mmap_device_ioctl(fl, ioctl_num, &p, param);
		break;
	default:
		err = -ENOTTY;
		pr_info("bad ioctl: %d\n", ioctl_num);
		break;
	}
 bail:
	return err;
}

static int fastrpc_restart_notifier_cb(struct notifier_block *nb,
					unsigned long code,
					void *data)
{
	struct fastrpc_apps *me = &gfa;
	struct fastrpc_channel_ctx *ctx;
	struct notif_data *notifdata = (struct notif_data *)data;
	int cid = -1;

	ctx = container_of(nb, struct fastrpc_channel_ctx, nb);
	cid = ctx - &me->channel[0];
	if (code == SUBSYS_BEFORE_SHUTDOWN) {
		pr_info("adsprpc: %s: %s subsystem is restarting\n",
			__func__, gcinfo[cid].subsys);
		mutex_lock(&me->channel[cid].smd_mutex);
		ctx->ssrcount++;
		ctx->issubsystemup = 0;
		mutex_unlock(&me->channel[cid].smd_mutex);
	} else if (code == SUBSYS_RAMDUMP_NOTIFICATION) {
		if (cid == RH_CID) {
			if (me->ramdump_handle)
				me->channel[RH_CID].ramdumpenabled = 1;
		}
		if (cid == CDSP_DOMAIN_ID)
			fastrpc_ramdump_collection(cid);
		pr_info("adsprpc: %s: received RAMDUMP notification for %s\n",
			__func__, gcinfo[cid].subsys);
	} else if (code == SUBSYS_BEFORE_POWERUP) {
		if (cid == RH_CID && notifdata->enable_ramdump) {
			if (me->ramdump_handle && me->channel[RH_CID]
					.ramdumpenabled) {
				me->enable_ramdump = true;
			me->channel[RH_CID].ramdumpenabled = 0;
			}
		}
		fastrpc_notify_drivers(me, cid);
	} else if (code == SUBSYS_AFTER_POWERUP) {
		pr_info("adsprpc: %s: %s subsystem is up\n",
			__func__, gcinfo[cid].subsys);
		ctx->issubsystemup = 1;
	}
	return NOTIFY_DONE;
}

static int fastrpc_pdr_notifier_cb(struct notifier_block *pdrnb,
					unsigned long code,
					void *data)
{
	struct fastrpc_apps *me = &gfa;
	struct fastrpc_static_pd *spd;
	struct notif_data *notifdata = (struct notif_data *)data;

	spd = container_of(pdrnb, struct fastrpc_static_pd, pdrnb);
	if (code == SERVREG_NOTIF_SERVICE_STATE_DOWN_V01) {
		pr_info("adsprpc: %s: %s (%s) is down for PDR on %s\n",
			__func__, spd->spdname, spd->servloc_name,
			gcinfo[spd->cid].subsys);
		mutex_lock(&me->channel[spd->cid].smd_mutex);
		spd->pdrcount++;
		spd->ispdup = 0;
		mutex_unlock(&me->channel[spd->cid].smd_mutex);
		if (!strcmp(spd->servloc_name,
				AUDIO_PDR_SERVICE_LOCATION_CLIENT_NAME))
			me->staticpd_flags = 0;
		fastrpc_notify_pdr_drivers(me, spd->servloc_name);
	} else if (code == SUBSYS_RAMDUMP_NOTIFICATION) {
		if (spd->cid == RH_CID) {
			if (spd->cid == RH_CID && me->ramdump_handle)
				me->channel[RH_CID].ramdumpenabled = 1;
		}
		pr_info("adsprpc: %s: received %s RAMDUMP notification for %s (%s)\n",
			__func__, gcinfo[spd->cid].subsys,
			spd->spdname, spd->servloc_name);
	} else if (code == SUBSYS_BEFORE_POWERUP) {
		if (spd->cid == RH_CID && notifdata->enable_ramdump) {
			if (me->ramdump_handle && me->channel[RH_CID]
					.ramdumpenabled)
				me->enable_ramdump = true;
			me->channel[RH_CID].ramdumpenabled = 0;
			}
	} else if (code == SERVREG_NOTIF_SERVICE_STATE_UP_V01) {
		pr_info("adsprpc: %s: %s (%s) is up on %s\n",
			__func__, spd->spdname, spd->servloc_name,
			gcinfo[spd->cid].subsys);
		spd->ispdup = 1;
	}

	return NOTIFY_DONE;
}

/*
 * The service locator callback function where the PDR notification
 * callback functions are registered.
 * (like audioPD on ADSP, sensorPD on SLPI/ADSP)
 */
static int fastrpc_get_service_location_notify(struct notifier_block *nb,
				unsigned long opcode, void *data)
{
	struct fastrpc_static_pd *spd;
	struct pd_qmi_client_data *pdr = data;
	int curr_state = 0, i = 0;
	char *cb_pdname = NULL, *subsys = NULL;
	uint32_t instance_id = 0;

	spd = container_of(nb, struct fastrpc_static_pd, get_service_nb);
	subsys = gcinfo[spd->cid].subsys;
	if (opcode == LOCATOR_DOWN) {
		pr_warn("adsprpc: %s: PDR notifier locator for %s is down for %s\n",
				__func__, subsys, spd->servloc_name);
		return NOTIFY_DONE;
	}
	for (i = 0; i < pdr->total_domains; i++) {
		cb_pdname = pdr->domain_list[i].name;
		instance_id = pdr->domain_list[i].instance_id;

		/* Check the client and staticPD in the callback */
		if (COMPARE_SERVICE_LOCATOR_NAMES(spd->servloc_name,
				AUDIO_PDR_SERVICE_LOCATION_CLIENT_NAME,
				cb_pdname, ADSP_AUDIOPD_NAME) ||

			COMPARE_SERVICE_LOCATOR_NAMES(spd->servloc_name,
				SENSORS_PDR_ADSP_SERVICE_LOCATION_CLIENT_NAME,
				cb_pdname, ADSP_SENSORPD_NAME) ||

			COMPARE_SERVICE_LOCATOR_NAMES(spd->servloc_name,
				SENSORS_PDR_SLPI_SERVICE_LOCATION_CLIENT_NAME,
				cb_pdname, SLPI_SENSORPD_NAME)) {

			goto pdr_register;
		}
	}
	return NOTIFY_DONE;

pdr_register:
	if (!spd->pdrhandle) {
		/* Register the PDR notifier callback function */
		spd->pdrhandle = service_notif_register_notifier(cb_pdname,
			instance_id, &spd->pdrnb, &curr_state);
		if (IS_ERR_OR_NULL(spd->pdrhandle))
			pr_warn("adsprpc: %s: PDR notifier for %s register failed for %s (%s) with err %ld\n",
				__func__, subsys, cb_pdname, spd->servloc_name,
				PTR_ERR(spd->pdrhandle));
		else
			pr_info("adsprpc: %s: PDR notifier for %s registered for %s (%s)\n",
			__func__, subsys, cb_pdname, spd->servloc_name);
	} else {
		pr_warn("adsprpc: %s: %s (%s) notifier is already registered for %s\n",
			__func__, cb_pdname, spd->servloc_name, subsys);
	}

	if (curr_state == SERVREG_NOTIF_SERVICE_STATE_UP_V01) {
		pr_info("adsprpc: %s: %s (%s) PDR service for %s is up\n",
			__func__, spd->servloc_name, cb_pdname, subsys);
		spd->ispdup = 1;
	} else if (curr_state == SERVREG_NOTIF_SERVICE_STATE_UNINIT_V01) {
		pr_info("adsprpc: %s: %s (%s) PDR service for %s is uninitialized\n",
			__func__, spd->servloc_name, cb_pdname, subsys);
	}
	return NOTIFY_DONE;
}

static const struct file_operations fops = {
	.open = fastrpc_device_open,
	.release = fastrpc_device_release,
	.unlocked_ioctl = fastrpc_device_ioctl,
	.compat_ioctl = compat_fastrpc_device_ioctl,
};

static const struct of_device_id fastrpc_match_table[] = {
	{ .compatible = "qcom,msm-fastrpc-adsp", },
	{ .compatible = "qcom,msm-fastrpc-compute", },
	{ .compatible = "qcom,msm-fastrpc-compute-cb", },
	{ .compatible = "qcom,msm-adsprpc-mem-region", },
	{}
};

static int fastrpc_cb_probe(struct device *dev)
{
	struct fastrpc_channel_ctx *chan;
	struct fastrpc_session_ctx *sess;
	struct of_phandle_args iommuspec;
	struct fastrpc_apps *me = &gfa;
	const char *name;
	int err = 0, cid = -1, i = 0;
	u32 sharedcb_count = 0, j = 0;
	uint32_t dma_addr_pool[2] = {0, 0};

	VERIFY(err, NULL != (name = of_get_property(dev->of_node,
					 "label", NULL)));
	if (err)
		goto bail;

	for (i = 0; i < NUM_CHANNELS; i++) {
		if (!gcinfo[i].name)
			continue;
		if (!strcmp(name, gcinfo[i].name))
			break;
	}
	VERIFY(err, i < NUM_CHANNELS);
	if (err) {
		err = -ECHRNG;
		goto bail;
	}
	cid = i;
	chan = &gcinfo[i];
	VERIFY(err, chan->sesscount < NUM_SESSIONS);
	if (err)
		goto bail;

	err = of_parse_phandle_with_args(dev->of_node, "iommus",
						"#iommu-cells", 0, &iommuspec);
	if (err) {
		pr_err("Error: adsprpc: %s: parsing iommu arguments failed for %s with err %d\n",
					__func__, dev_name(dev), err);
		goto bail;
	}
	sess = &chan->session[chan->sesscount];
	sess->used = 0;
#if IS_ENABLED(CONFIG_ADSPRPC_QGKI)
	/*
	 * On a GKI kernel, the DMA driver does not support I/O coherency and
	 * the fastrpc driver needs to do the cache maintenance. So this
	 * device-tree property needs to be read only on a QGKI kernel.
	 */
	sess->smmu.coherent = of_property_read_bool(dev->of_node,
						"dma-coherent-hint-cached");
#endif
	sess->smmu.secure = of_property_read_bool(dev->of_node,
						"qcom,secure-context-bank");
	sess->smmu.cb = iommuspec.args[0] & 0xf;
	sess->smmu.dev = dev;
	sess->smmu.dev_name = dev_name(dev);
	sess->smmu.enabled = 1;

	if (!sess->smmu.dev->dma_parms)
		sess->smmu.dev->dma_parms = devm_kzalloc(sess->smmu.dev,
			sizeof(*sess->smmu.dev->dma_parms), GFP_KERNEL);

	dma_set_max_seg_size(sess->smmu.dev, DMA_BIT_MASK(32));
	dma_set_seg_boundary(sess->smmu.dev, (unsigned long)DMA_BIT_MASK(64));

	of_property_read_u32_array(dev->of_node, "qcom,iommu-dma-addr-pool",
			dma_addr_pool, 2);
	me->max_size_limit = (dma_addr_pool[1] == 0 ? 0x78000000 :
			dma_addr_pool[1]);

	if (of_get_property(dev->of_node, "shared-cb", NULL) != NULL) {
		err = of_property_read_u32(dev->of_node, "shared-cb",
				&sharedcb_count);
		if (err)
			goto bail;
		if (sharedcb_count > 0) {
			struct fastrpc_session_ctx *dup_sess;

			for (j = 1; j < sharedcb_count &&
					chan->sesscount < NUM_SESSIONS; j++) {
				chan->sesscount++;
				dup_sess = &chan->session[chan->sesscount];
				memcpy(dup_sess, sess,
					sizeof(struct fastrpc_session_ctx));
			}
		}
	}

	chan->sesscount++;
	if (debugfs_root && !debugfs_global_file) {
		debugfs_global_file = debugfs_create_file("global", 0644,
			debugfs_root, NULL, &debugfs_fops);
		if (IS_ERR_OR_NULL(debugfs_global_file)) {
			pr_warn("Error: %s: %s: failed to create debugfs global file\n",
				current->comm, __func__);
			debugfs_global_file = NULL;
		}
	}
bail:
	return err;
}

static void init_secure_vmid_list(struct device *dev, char *prop_name,
						struct secure_vm *destvm)
{
	int err = 0;
	u32 len = 0, i = 0;
	u32 *rhvmlist = NULL;
	u32 *rhvmpermlist = NULL;

	if (!of_find_property(dev->of_node, prop_name, &len))
		goto bail;
	if (len == 0)
		goto bail;
	len /= sizeof(u32);
	VERIFY(err, NULL != (rhvmlist = kcalloc(len, sizeof(u32), GFP_KERNEL)));
	if (err)
		goto bail;
	VERIFY(err, NULL != (rhvmpermlist = kcalloc(len, sizeof(u32),
					 GFP_KERNEL)));
	if (err)
		goto bail;
	for (i = 0; i < len; i++) {
		err = of_property_read_u32_index(dev->of_node, prop_name, i,
								&rhvmlist[i]);
		if (err) {
			pr_err("Error: adsprpc: %s: failed to read VMID\n",
				__func__);
			goto bail;
		}
		ADSPRPC_INFO("secure VMID = %d\n",
			rhvmlist[i]);
		rhvmpermlist[i] = PERM_READ | PERM_WRITE | PERM_EXEC;
	}
	destvm->vmid = rhvmlist;
	destvm->vmperm = rhvmpermlist;
	destvm->vmcount = len;
bail:
	if (err) {
		kfree(rhvmlist);
		kfree(rhvmpermlist);
	}
}

static void init_qos_cores_list(struct device *dev, char *prop_name,
						struct qos_cores *silvercores)
{
	int err = 0;
	u32 len = 0, i = 0;
	u32 *coreslist = NULL;

	if (!of_find_property(dev->of_node, prop_name, &len))
		goto bail;
	if (len == 0)
		goto bail;
	len /= sizeof(u32);
	VERIFY(err, NULL != (coreslist = kcalloc(len, sizeof(u32),
						 GFP_KERNEL)));
	if (err)
		goto bail;
	for (i = 0; i < len; i++) {
		err = of_property_read_u32_index(dev->of_node, prop_name, i,
								&coreslist[i]);
		if (err) {
			pr_err("adsprpc: %s: failed to read QOS cores list\n",
								 __func__);
			goto bail;
		}
	}
	silvercores->coreno = coreslist;
	silvercores->corecount = len;
bail:
	if (err) {
		kfree(coreslist);
	}
}

static void fastrpc_init_privileged_gids(struct device *dev, char *prop_name,
						struct gid_list *gidlist)
{
	int err = 0;
	u32 len = 0, i = 0;
	u32 *gids = NULL;

	if (!of_find_property(dev->of_node, prop_name, &len))
		goto bail;
	if (len == 0)
		goto bail;
	len /= sizeof(u32);
	gids = kcalloc(len, sizeof(u32), GFP_KERNEL);
	if (!gids) {
		err = ENOMEM;
		goto bail;
	}
	for (i = 0; i < len; i++) {
		err = of_property_read_u32_index(dev->of_node, prop_name,
								i, &gids[i]);
		if (err) {
			pr_err("Error: adsprpc: %s: failed to read GID %u\n",
					__func__, i);
			goto bail;
		}
		pr_info("adsprpc: %s: privileged GID: %u\n", __func__, gids[i]);
	}
	sort(gids, len, sizeof(*gids), uint_cmp_func, NULL);
	gidlist->gids = gids;
	gidlist->gidcount = len;
bail:
	if (err)
		kfree(gids);
}

static void configure_secure_channels(uint32_t secure_domains)
{
	struct fastrpc_apps *me = &gfa;
	int ii = 0;
	/*
	 * secure_domains contains the bitmask of the secure channels
	 *  Bit 0 - ADSP
	 *  Bit 1 - MDSP
	 *  Bit 2 - SLPI
	 *  Bit 3 - CDSP
	 */
	for (ii = ADSP_DOMAIN_ID; ii <= CDSP_DOMAIN_ID; ++ii) {
		int secure = (secure_domains >> ii) & 0x01;

		me->channel[ii].secure = secure;
		ADSPRPC_INFO("domain %d configured as secure %d\n", ii, secure);
	}
}

/*
 * This function is used to create the service locator required for
 * registering for remote process restart (PDR) notifications if that
 * PDR property has been enabled in the fastrpc node on the DTSI.
 */
static int fastrpc_setup_service_locator(struct device *dev,
	const char *propname, char *client_name, char *service_name)
{
	int err = 0, session = -1, cid = -1;
	struct fastrpc_apps *me = &gfa;

	if (of_property_read_bool(dev->of_node, propname)) {
		err = fastrpc_get_spd_session(client_name, &session, &cid);
		if (err)
			goto bail;
		/* Register the service locator's callback function */
		me->channel[cid].spd[session].get_service_nb.notifier_call =
					fastrpc_get_service_location_notify;
		err = get_service_location(client_name, service_name,
				&me->channel[cid].spd[session].get_service_nb);
		if (err)
			pr_warn("adsprpc: %s: get service location failed with %d for %s (%s)\n",
				__func__, err, service_name, client_name);
		else
			pr_info("adsprpc: %s: service location enabled for %s (%s)\n",
				__func__, service_name, client_name);
	}
bail:
	return err;
}

/*
 * name : remote_cdsp_status_show
 * @in  : dev : pointer to device node
 *        attr: pointer to device attribute
 * @out : buf : Contains remote cdsp status
 * @Description : This function updates the buf with
 * remote cdsp status by reading the fastrpc node
 * @returns : bytes written to buf
 */

static ssize_t remote_cdsp_status_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct fastrpc_apps *me = &gfa;

	/*
	 * Default remote DSP status: 0
	 * driver possibly not probed yet or not the main device.
	 */

	if (!dev || !dev->driver ||
		!of_device_is_compatible(dev->of_node, "qcom,msm-fastrpc-compute")) {
		ADSPRPC_ERR(
			"adsprpc: Error: %s: driver not probed yet or not the main device\n",
			__func__);
		return 0;
	}

	return scnprintf(buf, PAGE_SIZE, "%d",
			me->remote_cdsp_status);
}

/* Remote cdsp status attribute declartion as read only */
static DEVICE_ATTR_RO(remote_cdsp_status);

/* Declaring attribute for remote dsp */
static struct attribute *msm_remote_dsp_attrs[] = {
	&dev_attr_remote_cdsp_status.attr,
	NULL
};

/* Defining remote dsp attributes in attributes group */
static struct attribute_group msm_remote_dsp_attr_group = {
	.attrs = msm_remote_dsp_attrs,
};

static int fastrpc_probe(struct platform_device *pdev)
{
	int err = 0;
	struct fastrpc_apps *me = &gfa;
	struct device *dev = &pdev->dev;
	int ret = 0;
	uint32_t secure_domains = 0;

	if (of_device_is_compatible(dev->of_node,
					"qcom,msm-fastrpc-compute")) {
		me->dev_fastrpc = dev;
		err = sysfs_create_group(&pdev->dev.kobj, &msm_remote_dsp_attr_group);
		if (err) {
			ADSPRPC_ERR(
				"adsprpc: Error: %s: initialization of sysfs create group failed with %d\n",
				__func__, err);
			goto bail;
		}
		init_secure_vmid_list(dev, "qcom,adsp-remoteheap-vmid",
							&gcinfo[0].rhvm);
		fastrpc_init_privileged_gids(dev, "qcom,fastrpc-gids",
					&me->gidlist);
		init_qos_cores_list(dev, "qcom,qos-cores",
							&me->silvercores);

		of_property_read_u32(dev->of_node, "qcom,rpc-latency-us",
			&me->latency);
		if (of_get_property(dev->of_node,
			"qcom,secure-domains", NULL) != NULL) {
			VERIFY(err, !of_property_read_u32(dev->of_node,
					  "qcom,secure-domains",
			      &secure_domains));
			if (!err)
				configure_secure_channels(secure_domains);
			else
				pr_info("adsprpc: unable to read the domain configuration from dts\n");
		}
	}
	if (of_device_is_compatible(dev->of_node,
					"qcom,msm-fastrpc-compute-cb"))
		return fastrpc_cb_probe(dev);

	if (of_device_is_compatible(dev->of_node,
					"qcom,msm-adsprpc-mem-region")) {
		me->dev = dev;
		ret = of_reserved_mem_device_init_by_idx(dev, dev->of_node, 0);
		if (ret) {
			pr_warn("adsprpc: Error: %s: initialization of memory region adsp_mem failed with %d\n",
				__func__, ret);
		}
		me->ramdump_handle = create_ramdump_device("adsp_rh", &pdev->dev);
		goto bail;
	}
	me->legacy_remote_heap = of_property_read_bool(dev->of_node,
					"qcom,fastrpc-legacy-remote-heap");

	fastrpc_setup_service_locator(dev, AUDIO_PDR_ADSP_DTSI_PROPERTY_NAME,
		AUDIO_PDR_SERVICE_LOCATION_CLIENT_NAME,
		AUDIO_PDR_ADSP_SERVICE_NAME);
	fastrpc_setup_service_locator(dev, SENSORS_PDR_ADSP_DTSI_PROPERTY_NAME,
		SENSORS_PDR_ADSP_SERVICE_LOCATION_CLIENT_NAME,
		SENSORS_PDR_ADSP_SERVICE_NAME);
	fastrpc_setup_service_locator(dev, SENSORS_PDR_SLPI_DTSI_PROPERTY_NAME,
		SENSORS_PDR_SLPI_SERVICE_LOCATION_CLIENT_NAME,
		SENSORS_PDR_SLPI_SERVICE_NAME);

	err = of_platform_populate(pdev->dev.of_node,
					  fastrpc_match_table,
					  NULL, &pdev->dev);
	if (err)
		goto bail;
bail:
	return err;
}

static void fastrpc_deinit(void)
{
	struct fastrpc_channel_ctx *chan = gcinfo;
	struct fastrpc_apps *me = &gfa;
	int i, j;

	for (i = 0; i < NUM_CHANNELS; i++, chan++) {
		for (j = 0; j < NUM_SESSIONS; j++) {
			struct fastrpc_session_ctx *sess = &chan->session[j];

			if (sess->smmu.dev)
				sess->smmu.dev = NULL;
		}
		kfree(chan->rhvm.vmid);
		kfree(chan->rhvm.vmperm);
	}
	mutex_destroy(&me->mut_uid);
}

static struct platform_driver fastrpc_driver = {
	.probe = fastrpc_probe,
	.driver = {
		.name = "fastrpc",
		.of_match_table = fastrpc_match_table,
		.suppress_bind_attrs = true,
	},
};

static const struct rpmsg_device_id fastrpc_rpmsg_match[] = {
	{ FASTRPC_GLINK_GUID },
	{ },
};

static const struct of_device_id fastrpc_rpmsg_of_match[] = {
	{ .compatible = "qcom,msm-fastrpc-rpmsg" },
	{ },
};
MODULE_DEVICE_TABLE(of, fastrpc_rpmsg_of_match);

static struct rpmsg_driver fastrpc_rpmsg_client = {
	.id_table = fastrpc_rpmsg_match,
	.probe = fastrpc_rpmsg_probe,
	.remove = fastrpc_rpmsg_remove,
	.callback = fastrpc_rpmsg_callback,
	.drv = {
		.name = "qcom,msm_fastrpc_rpmsg",
		.of_match_table = fastrpc_rpmsg_of_match,
	},
};

static int __init fastrpc_device_init(void)
{
	struct fastrpc_apps *me = &gfa;
	int err = 0, i;

#ifdef CONFIG_DEBUG_FS
	debugfs_root = debugfs_create_dir("adsprpc", NULL);
	if (IS_ERR_OR_NULL(debugfs_root)) {
		pr_warn("Error: %s: %s: failed to create debugfs root dir\n",
			current->comm, __func__);
		debugfs_remove_recursive(debugfs_root);
		debugfs_root = NULL;
	}
#endif
	memset(me, 0, sizeof(*me));
	fastrpc_init(me);
	fastrpc_get_dsp_status(me);
	me->dev = NULL;
	me->legacy_remote_heap = false;
	VERIFY(err, 0 == platform_driver_register(&fastrpc_driver));
	if (err)
		goto register_bail;
	VERIFY(err, 0 == alloc_chrdev_region(&me->dev_no, 0, NUM_CHANNELS,
		DEVICE_NAME));
	if (err)
		goto alloc_chrdev_bail;
	cdev_init(&me->cdev, &fops);
	me->cdev.owner = THIS_MODULE;
	VERIFY(err, 0 == cdev_add(&me->cdev, MKDEV(MAJOR(me->dev_no), 0),
				NUM_DEVICES));
	if (err)
		goto cdev_init_bail;
	me->class = class_create(THIS_MODULE, "fastrpc");
	VERIFY(err, !IS_ERR(me->class));
	if (err)
		goto class_create_bail;
	me->compat = (fops.compat_ioctl == NULL) ? 0 : 1;

	/*
	 * Create devices and register with sysfs
	 * Create first device with minor number 0
	 */
	me->non_secure_dev = device_create(me->class, NULL,
				MKDEV(MAJOR(me->dev_no), MINOR_NUM_DEV),
				NULL, DEVICE_NAME);
	VERIFY(err, !IS_ERR_OR_NULL(me->non_secure_dev));
	if (err) {
		err = -ENODEV;
		goto device_create_bail;
	}

	/* Create secure device with minor number for secure device */
	me->secure_dev = device_create(me->class, NULL,
				MKDEV(MAJOR(me->dev_no), MINOR_NUM_SECURE_DEV),
				NULL, DEVICE_NAME_SECURE);
	VERIFY(err, !IS_ERR_OR_NULL(me->secure_dev));
	if (err)
		goto device_create_bail;

	for (i = 0; i < NUM_CHANNELS; i++) {
		me->jobid[i] = 1;
		me->channel[i].dev = me->secure_dev;
		if (i == CDSP_DOMAIN_ID)
			me->channel[i].dev = me->non_secure_dev;
		me->channel[i].ssrcount = 0;
		me->channel[i].prevssrcount = 0;
		me->channel[i].issubsystemup = 1;
		me->channel[i].ramdumpenabled = 0;
		me->channel[i].rh_dump_dev = NULL;
		me->channel[i].nb.notifier_call = fastrpc_restart_notifier_cb;
		me->channel[i].handle = subsys_notif_register_notifier(
							gcinfo[i].subsys,
							&me->channel[i].nb);
		if (IS_ERR_OR_NULL(me->channel[i].handle))
			pr_warn("adsprpc: %s: SSR notifier register failed for %s with err %d\n",
				__func__, gcinfo[i].subsys,
				PTR_ERR(me->channel[i].handle));
		else
			pr_info("adsprpc: %s: SSR notifier registered for %s\n",
				__func__, gcinfo[i].subsys);
	}
	me->channel[CDSP_DOMAIN_ID].rh_dump_dev = create_ramdump_device("cdsp_minidump", NULL);
	err = register_rpmsg_driver(&fastrpc_rpmsg_client);
	if (err) {
		pr_err("Error: adsprpc: %s: register_rpmsg_driver failed with err %d\n",
			__func__, err);
		goto device_create_bail;
	}
	me->rpmsg_register = 1;

	fastrpc_register_wakeup_source(me->non_secure_dev,
		FASTRPC_NON_SECURE_WAKE_SOURCE_CLIENT_NAME,
		&me->wake_source);
	fastrpc_register_wakeup_source(me->secure_dev,
		FASTRPC_SECURE_WAKE_SOURCE_CLIENT_NAME,
		&me->wake_source_secure);

	return 0;
device_create_bail:
	for (i = 0; i < NUM_CHANNELS; i++) {
		if (me->channel[i].handle)
			subsys_notif_unregister_notifier(me->channel[i].handle,
							&me->channel[i].nb);
	}
	if (!IS_ERR_OR_NULL(me->non_secure_dev))
		device_destroy(me->class, MKDEV(MAJOR(me->dev_no),
						MINOR_NUM_DEV));
	if (!IS_ERR_OR_NULL(me->secure_dev))
		device_destroy(me->class, MKDEV(MAJOR(me->dev_no),
						 MINOR_NUM_SECURE_DEV));
	class_destroy(me->class);
class_create_bail:
	cdev_del(&me->cdev);
cdev_init_bail:
	unregister_chrdev_region(me->dev_no, NUM_CHANNELS);
alloc_chrdev_bail:
register_bail:
	fastrpc_deinit();
	return err;
}

static void __exit fastrpc_device_exit(void)
{
	struct fastrpc_apps *me = &gfa;
	int i;

	fastrpc_file_list_dtor(me);
	fastrpc_deinit();
	wakeup_source_unregister(me->wake_source);
	wakeup_source_unregister(me->wake_source_secure);
	for (i = 0; i < NUM_CHANNELS; i++) {
		if (!gcinfo[i].name)
			continue;
		subsys_notif_unregister_notifier(me->channel[i].handle,
						&me->channel[i].nb);
	}

	/* Destroy the secure and non secure devices */
	device_destroy(me->class, MKDEV(MAJOR(me->dev_no), MINOR_NUM_DEV));
	device_destroy(me->class, MKDEV(MAJOR(me->dev_no),
					 MINOR_NUM_SECURE_DEV));

	of_reserved_mem_device_release(me->dev);
	class_destroy(me->class);
	cdev_del(&me->cdev);
	unregister_chrdev_region(me->dev_no, NUM_CHANNELS);
	if (me->rpmsg_register == 1)
		unregister_rpmsg_driver(&fastrpc_rpmsg_client);
	kfree(me->gidlist.gids);
	debugfs_remove_recursive(debugfs_root);
}

module_init(fastrpc_device_init);
module_exit(fastrpc_device_exit);

MODULE_LICENSE("GPL v2");
