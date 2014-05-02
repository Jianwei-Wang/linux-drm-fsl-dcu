/*
 * Copyright © 2014 Red Hat.
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that copyright
 * notice and this permission notice appear in supporting documentation, and
 * that the name of the copyright holders not be used in advertising or
 * publicity pertaining to distribution of the software without specific,
 * written prior permission.  The copyright holders make no representations
 * about the suitability of this software for any purpose.  It is provided "as
 * is" without express or implied warranty.
 *
 * THE COPYRIGHT HOLDERS DISCLAIM ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL THE COPYRIGHT HOLDERS BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE
 * OF THIS SOFTWARE.
 */
#ifndef _DRM_DP_MST_HELPER_H_
#define _DRM_DP_MST_HELPER_H_

#include <linux/types.h>
#include <drm/drm_dp_helper.h>

struct drm_dp_mst_branch;

/* vpci channel info */
struct drm_dp_vcpi {
	int vcpi;
	int pbn;
	int aligned_pbn;
	int num_slots;
	struct list_head next;
};

/* display port MST port */
struct drm_dp_mst_port {
	struct kref kref;

	/* if dpcd 1.2 device is on this port - its GUID info */
	bool guid_valid;
	u8 guid[16];

	u8 port_num;
	bool input;
	bool mcs;
	bool ddps;
	u8 pdt;
	bool ldps;
	u8 dpcd_rev;
	uint8_t dpcd[DP_RECEIVER_CAP_SIZE];
	uint16_t available_pbn;
	struct list_head next;
	struct drm_dp_mst_branch *mstb; /* pointer to an mstb if this port has one */
	struct drm_dp_aux aux; /* i2c bus for this port? */
	struct drm_dp_mst_branch *parent;

	struct drm_dp_vcpi vcpi;
	struct drm_connector *connector;
};

/* list of MST branch devices we know about */
/* we can only have two outstanding un ack msgs for
   each MST device */
struct drm_dp_mst_branch {
	struct kref kref;
	u8 rad[8];
	u8 lct;
	int num_ports;

	int msg_slots;
	struct list_head ports;
	u8 fail;
	/* list of tx ops queue for this port */
	bool parent_is_port;
	struct drm_dp_mst_port *port_parent;
	struct drm_dp_mst_topology_mgr *mgr;

	spinlock_t slots_lock;
	struct drm_dp_sideband_msg_tx *tx_slots[2];
};


/* sideband msg header - not bit struct */
struct drm_dp_sideband_msg_hdr {
	u8 lct;
	u8 lcr;
	u8 rad[8];
	bool broadcast;
	bool path_msg;
	u8 msg_len;
	bool somt;
	bool eomt;
	bool seqno;
};

struct drm_dp_nak_reply {
	u8 guid[16];
	u8 reason;
	u8 nak_data;
};

struct drm_dp_link_address_ack_reply {
	u8 guid[16];
	u8 nports;
	struct drm_dp_link_addr_reply_port {
		bool input_port;
		u8 peer_device_type;
		u8 port_number;
		bool mcs;
		bool ddps;
		bool legacy_device_plug_status;
		u8 dpcd_revision;
		u8 peer_guid[16];
		bool num_sdp_streams;
		bool num_sdp_stream_sinks;
	} ports[16];
};

struct drm_dp_remote_dpcd_read_ack_reply {
	u8 port_number;
	u8 num_bytes;
	u8 bytes[255];
};

struct drm_dp_remote_dpcd_write_ack_reply {
	u8 port_number;
};

struct drm_dp_remote_dpcd_write_nak_reply {
	u8 port_number;
	u8 reason;
	u8 bytes_written_before_failure;
};

struct drm_dp_remote_i2c_read_ack_reply {
	u8 port_number;
	u8 num_bytes;
	u8 bytes[255];
};

struct drm_dp_remote_i2c_read_nak_reply {
	u8 port_number;
	u8 nak_reason;
	u8 i2c_nak_transaction;
};

struct drm_dp_remote_i2c_write_ack_reply {
	u8 port_number;
};


struct drm_dp_sideband_msg_rx {
	u8 chunk[48];
	u8 msg[256];
	u8 curchunk_len;
	u8 curchunk_idx; /* chunk we are parsing now */
	u8 curchunk_hdrlen;
	u8 curlen; /* total length of the msg */
	bool have_somt;
	bool have_eomt;
	struct drm_dp_sideband_msg_hdr initial_hdr;
};


struct drm_dp_allocate_payload {
	u8 port_number;
	u8 number_sdp_streams;
	u8 vcpi;
	u16 pbn;
	u8 sdp_stream_sink[8];
};

struct drm_dp_allocate_payload_ack_reply {
	u8 port_number;
	u8 vcpi;
	u16 allocated_pbn;
};

struct drm_dp_connection_status_notify {
	u8 guid[16];
	u8 port_number;
	bool legacy_device_plug_status;
	bool displayport_device_plug_status;
	bool message_capability_status;
	bool input_port;
	u8 peer_device_type;
};

struct drm_dp_remote_dpcd_read {
	u8 port_number;
	u32 dpcd_address;
	u8 num_bytes;
};

struct drm_dp_remote_dpcd_write {
	u8 port_number;
	u32 dpcd_address;
	u8 num_bytes;
	u8 bytes[255];
};

struct drm_dp_remote_i2c_read {
	u8 num_transactions;
	u8 port_number;
	struct {
		u8 i2c_dev_id;
		u8 num_bytes;
		u8 bytes[255];
		u8 no_stop_bit;
		u8 i2c_transaction_delay;
	} transactions[4];
	u8 read_i2c_device_id;
	u8 num_bytes_read;
};

struct drm_dp_remote_i2c_write {
	u8 port_number;
	u8 write_i2c_device_id;
	u8 num_bytes;
	u8 bytes[255];
};

/* this covers ENUM_RESOURCES, POWER_DOWN_PHY, POWER_UP_PHY */
struct drm_dp_port_number_req {
	u8 port_number;
};

struct drm_dp_enum_path_resources_ack_reply {
	u8 port_number;
	u16 full_payload_bw_number;
	u16 avail_payload_bw_number;
};

/* covers POWER_DOWN_PHY, POWER_UP_PHY */
struct drm_dp_port_number_rep {
	u8 port_number;
};

struct drm_dp_query_payload {
	u8 port_number;
	u8 vcpi;
};

struct drm_dp_resource_status_notify {
	u8 port_number;
	u8 guid[16];
	u16 available_pbn;
};

struct drm_dp_query_payload_ack_reply {
	u8 port_number;
	u8 allocated_pbn;
};

struct drm_dp_sideband_msg_req_body {
	u8 req_type;
	union ack_req {
		struct drm_dp_connection_status_notify conn_stat;
		struct drm_dp_port_number_req port_num;
		struct drm_dp_resource_status_notify resource_stat;

		struct drm_dp_query_payload query_payload;
		struct drm_dp_allocate_payload allocate_payload;

		struct drm_dp_remote_dpcd_read dpcd_read;
		struct drm_dp_remote_dpcd_write dpcd_write;

		struct drm_dp_remote_i2c_read i2c_read;
		struct drm_dp_remote_i2c_write i2c_write;
	} u;
};

struct drm_dp_sideband_msg_reply_body {
	u8 reply_type;
	u8 req_type;
	union ack_replies {
		struct drm_dp_nak_reply nak;
		struct drm_dp_link_address_ack_reply link_addr;
		struct drm_dp_port_number_rep port_number;

		struct drm_dp_enum_path_resources_ack_reply path_resources;
		struct drm_dp_allocate_payload_ack_reply allocate_payload;
		struct drm_dp_query_payload_ack_reply query_payload;

		struct drm_dp_remote_dpcd_read_ack_reply remote_dpcd_read_ack;
		struct drm_dp_remote_dpcd_write_ack_reply remote_dpcd_write_ack;
		struct drm_dp_remote_dpcd_write_nak_reply remote_dpcd_write_nack;

		struct drm_dp_remote_i2c_read_ack_reply remote_i2c_read_ack;
		struct drm_dp_remote_i2c_read_nak_reply remote_i2c_read_nack;
		struct drm_dp_remote_i2c_write_ack_reply remote_i2c_write_ack;
	} u;
};

/* msg is queued to be put into a slot */
#define DRM_DP_SIDEBAND_TX_QUEUED 0
/* msg has started transmitting on a slot - still on msgq */
#define DRM_DP_SIDEBAND_TX_START_SEND 1
/* msg has finished transmitting on a slot - removed from msgq only in slot */
#define DRM_DP_SIDEBAND_TX_SENT 2
/* msg has received a response - removed from slot */
#define DRM_DP_SIDEBAND_TX_RX 3
#define DRM_DP_SIDEBAND_TX_TIMEOUT 4

struct drm_dp_sideband_msg_tx {
	u8 msg[256];
	u8 chunk[48];
	u8 cur_offset;
	u8 cur_len;
	struct drm_dp_mst_branch *dst;
	struct list_head next;
	int seqno;
	int state;
	struct drm_dp_sideband_msg_reply_body reply;
};

/* sideband msg handler */
struct drm_dp_mst_topology_mgr;
struct drm_dp_mst_topology_cbs {
	/* create a connector for a port */
	struct drm_connector *(*add_connector)(struct drm_dp_mst_topology_mgr *mgr, struct drm_dp_mst_port *port);
	void (*destroy_connector)(struct drm_dp_mst_topology_mgr *mgr,
				  struct drm_connector *connector);
	void (*generate_act_event)(struct drm_dp_mst_topology_mgr *mgr);
};

#define DP_MAX_PAYLOAD (sizeof(unsigned long) * 8)

#define DP_PAYLOAD_LOCAL 1
#define DP_PAYLOAD_REMOTE 2

struct drm_dp_payload {
	int payload_state;
	int start_slot;
	int num_slots;
	struct drm_dp_mst_port *port;
};

struct drm_dp_mst_topology_mgr {
	struct device *dev;
	struct drm_dp_mst_topology_cbs *cbs;
	bool mst_state;
	int max_dpcd_transaction_bytes;
	struct drm_dp_aux *aux; /* auxch for this topology mgr to use */

	struct mutex lock;
	struct drm_dp_sideband_msg_rx down_rep_recv;
	struct drm_dp_sideband_msg_rx up_req_recv;

	/* pointer to info about the initial MST device */
	struct drm_dp_mst_branch *mst_primary;

	/* primary MST device GUID */
	bool guid_valid;
	u8 guid[16];

	/* messages to be transmitted */
	struct list_head tx_msg_downq;
	struct list_head tx_msg_upq;
	bool tx_down_in_progress;
	bool tx_up_in_progress;

	struct drm_dp_vcpi **proposed_vcpis;
	struct drm_dp_payload *payloads;

	int max_payloads;
	unsigned long payload_mask;
	/* protect the upq/downq and in_progress */
	spinlock_t qlock;

	wait_queue_head_t tx_waitq;
	struct work_struct work;

	u8 dpcd[DP_RECEIVER_CAP_SIZE];
	int pbn_div;
	int total_pbn;
	int total_slots;
	int avail_slots;
};

int drm_dp_mst_topology_mgr_init(struct drm_dp_mst_topology_mgr *mgr, struct device *dev, struct drm_dp_aux *aux, int max_dpcd_transaction_bytes, int max_payloads);
void drm_dp_mst_topology_mgr_destroy(struct drm_dp_mst_topology_mgr *mgr);
int drm_dp_mst_topology_mgr_set_mst(struct drm_dp_mst_topology_mgr *mgr, bool mst_state);
int drm_dp_mst_hpd_irq(struct drm_dp_mst_topology_mgr *mgr, int irq_vector);

/* hacky interface for now */
enum drm_connector_status drm_dp_mst_detect_port(struct drm_dp_mst_topology_mgr *mgr, struct drm_dp_mst_port *port);
struct drm_connector;
struct edid *drm_dp_mst_get_edid(struct drm_connector *connector, struct drm_dp_mst_topology_mgr *mgr, struct drm_dp_mst_port *port);
int drm_dp_calc_pbn_mode(int clock, int bpp);

bool drm_dp_mst_allocate_vcpi(struct drm_dp_mst_topology_mgr *mgr, struct drm_dp_mst_port *port, int pbn, int *slots, int force_override);
void drm_dp_mst_deallocate_vcpi(struct drm_dp_mst_topology_mgr *mgr,
				struct drm_dp_mst_port *port);
int drm_dp_update_payload_part1(struct drm_dp_mst_topology_mgr *mgr);
int drm_dp_update_payload_part2(struct drm_dp_mst_topology_mgr *mgr);
int drm_dp_check_act_status(struct drm_dp_mst_topology_mgr *mgr);

#endif
