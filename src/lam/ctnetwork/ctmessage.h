/*
 * $HEADER$
 */

#ifndef LAM_CT_MESSAGE_H
#define LAM_CT_MESSAGE_H

#include "lam/lfc/lam_object.h"


/*
 *
 *      Available Classes
 *
 */

extern lam_class_info_t     lam_ct_ctrl_t_class_info;
extern lam_class_info_t     lam_ctmsg_t_class_info;

/*
 *
 *      CT Message interface
 *
 */

/*
 * Message control info for routing msgs.
 */


/*
 * Message routing type
 */
 
enum
{
    LAM_CT_BCAST = 1,
    LAM_CT_ALLGATHER,
    LAM_CT_SCATTER,
    LAM_CT_PT2PT
};

/*
 * Msg control interface to
 * contain routing information.
 * Can be reused by multiple msgs
 * if routing info is the same.
 *
 */

typedef struct lam_ct_ctrl
{
    lam_object_t    super;
    uint16_t        ctc_is_user_msg;    /* 1 -> msg is for user app. */
    uint16_t        ctc_routing_type;   /* broadcast, scatter, pt2pt, etc. */
    uint32_t        ctc_sender;         /* node that initiated send. */
    uint32_t        ctc_dest;           /* destination node. */
    uint32_t        ctc_forwarding;     /* node that is relaying msg. */
    uint32_t        ctc_client_tag;     /* tag if client sent msg. */
    uint32_t        ctc_info_len;
    uint8_t         *ctc_info;
} lam_ct_ctrl_t;


void lam_ctc_construct(lam_ct_ctrl_t *ctrl);
void lam_ctc_destruct(lam_ct_ctrl_t *ctrl);

void lam_ctc_construct_with(lam_ct_ctrl_t *ctrl, int routing_type,
                       uint32_t sender,
                       uint32_t dest);

uint32_t lam_ctc_pack_size(lam_ct_ctrl_t *ctrl);
uint8_t *lam_ctc_pack(lam_ct_ctrl_t *ctrl, uint32_t *len);
lam_ct_ctrl_t *lam_ctc_unpack(uint8_t *buffer);

int lam_ctc_pack_buffer(lam_ct_ctrl_t *ctrl, uint8_t *buffer, uint32_t *len);
int lam_ctc_unpack_buffer(lam_ct_ctrl_t *ctrl, uint8_t *buffer, uint32_t *len);

/*
 * Functions for accessing data in packed
 * control struct.
 */

uint16_t lam_pk_ctc_is_user_msg(uint8_t *buffer);

uint16_t lam_pk_ctc_get_routing_type(uint8_t *buffer);

uint32_t lam_pk_ctc_get_sender(uint8_t *buffer);

uint32_t lam_pk_ctc_get_dest(uint8_t *buffer);

uint32_t lam_pk_ctc_get_forwarding(uint8_t *buffer);
void lam_pk_ctc_set_forwarding(uint8_t *buffer, uint32_t node);

uint8_t *lam_pk_ctc_get_info(uint8_t *buffer, uint32_t *len);
void lam_pk_ctc_set_info(uint8_t *buffer, uint8_t *info);

/*
 * Accessor functions
 */

bool lam_ctc_get_is_user_msg(lam_ct_ctrl_t *ctrl);
inline bool lam_ctc_get_is_user_msg(lam_ct_ctrl_t *ctrl)
{
  return ctrl->ctc_is_user_msg;
}
void lam_ctc_set_is_user_msg(lam_ct_ctrl_t *ctrl, bool yn);
inline void lam_ctc_set_is_user_msg(lam_ct_ctrl_t *ctrl, bool yn)
{
    ctrl->ctc_is_user_msg = yn;
}

uint16_t lam_ctc_get_routing_type(lam_ct_ctrl_t *ctrl);
inline uint16_t lam_ctc_get_routing_type(lam_ct_ctrl_t *ctrl) 
{
  return ctrl->ctc_routing_type;
}
void lam_ctc_set_routing_type(lam_ct_ctrl_t *ctrl, int rtype);
inline void lam_ctc_set_routing_type(lam_ct_ctrl_t *ctrl, int rtype)
{
    ctrl->ctc_routing_type = rtype;
}

uint32_t lam_ctc_get_sender(lam_ct_ctrl_t *ctrl);
inline uint32_t lam_ctc_get_sender(lam_ct_ctrl_t *ctrl) 
{
  return ctrl->ctc_sender;
}
void lam_ctc_set_sender(lam_ct_ctrl_t *ctrl, uint32_t sender);
inline void lam_ctc_set_sender(lam_ct_ctrl_t *ctrl, uint32_t sender)
{
    ctrl->ctc_sender = sender;
}

uint32_t lam_ctc_get_dest(lam_ct_ctrl_t *ctrl);
inline uint32_t lam_ctc_get_dest(lam_ct_ctrl_t *ctrl) 
{
  return ctrl->ctc_dest;
}
void lam_ctc_set_dest(lam_ct_ctrl_t *ctrl, uint32_t dest);
inline void lam_ctc_set_dest(lam_ct_ctrl_t *ctrl, uint32_t dest)
{
    ctrl->ctc_dest = dest;
}

uint32_t lam_ctc_get_forwarding(lam_ct_ctrl_t *ctrl);
inline uint32_t lam_ctc_get_forwarding(lam_ct_ctrl_t *ctrl) 
{
  return ctrl->ctc_forwarding;
}
void lam_ctc_set_forwarding(lam_ct_ctrl_t *ctrl, uint32_t node);
inline void lam_ctc_set_forwarding(lam_ct_ctrl_t *ctrl, uint32_t node)
{
    ctrl->ctc_forwarding = node;
}

uint8_t *lam_ctc_get_info(lam_ct_ctrl_t *ctrl, uint32_t *len);
inline uint8_t *lam_ctc_get_info(lam_ct_ctrl_t *ctrl, uint32_t *len)
{
    *len = ctrl->ctc_info_len;
    return ctrl->ctc_info;
}

void lam_ctc_set_info(lam_ct_ctrl_t *ctrl, uint32_t len, uint8_t *info);
inline void lam_ctc_set_info(lam_ct_ctrl_t *ctrl, uint32_t len, uint8_t *info)
{
    ctrl->ctc_info_len = len;
    ctrl->ctc_info = info;
}



/*
 *
 *  Message interface
 *
 */


typedef struct lam_ctmsg
{
    lam_object_t    super;
    lam_ct_ctrl_t   *ctm_ctrl;
    uint32_t        ctm_len;
    uint8_t         *ctm_data;
    int             ctm_should_free;
} lam_ctmsg_t;


void lam_ctm_construct(lam_ctmsg_t *msg);
void lam_ctm_destruct(lam_ctmsg_t *msg);

lam_ctmsg_t *lam_ctm_create_with(int is_user_msg, int routing_type,
                                 uint32_t sender,
                                 uint32_t dest, uint8_t *data,
                                 uint32_t data_len,
                                 int should_free);

uint8_t *lam_ctm_pack(lam_ctmsg_t *msg);
lam_ctmsg_t *lam_ctm_unpack(uint8_t *buffer);

/*
 * Functions for accessing data in packed
 * msg struct.
 */

uint8_t *lam_pk_ctm_get_control(uint8_t *buffer);
uint8_t *lam_pk_ctm_get_data(uint8_t *buffer, uint32_t *len);

/*
 *
 * Accessor functions
 *
 */

lam_ct_ctrl_t *lam_ctm_get_control(lam_ctmsg_t *msg);
inline lam_ct_ctrl_t *lam_ctm_get_control(lam_ctmsg_t *msg) 
{
  return msg->ctm_ctrl;
}
void lam_ctm_set_control(lam_ctmsg_t *msg, lam_ct_ctrl_t *ctrl);
inline void lam_ctm_set_control(lam_ctmsg_t *msg, lam_ct_ctrl_t *ctrl)
{
    msg->ctm_ctrl = ctrl;
}


#endif  /* LAM_CT_MESSAGE_H */
