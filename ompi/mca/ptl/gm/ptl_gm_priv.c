/* -*- Mode: C; c-basic-offset:4 ; -*- */

/*
 * Copyright (c) 2004-2005 The Trustees of Indiana University.
 *                         All rights reserved.
 * Copyright (c) 2004-2005 The Trustees of the University of Tennessee.
 *                         All rights reserved.
 * Copyright (c) 2004 The Ohio State University.
 *                    All rights reserved.
 * Copyright (c) 2004-2005 High Performance Computing Center Stuttgart,
 *                         University of Stuttgart.  All rights reserved.
 * Copyright (c) 2004-2005 The Regents of the University of California.
 *                         All rights reserved.
 * $COPYRIGHT$
 *
 * Additional copyrights may follow
 *
 * $HEADER$
 */
#include "ompi_config.h"

#include "include/types.h"
#include "mca/ptl/base/ptl_base_sendreq.h"
#include "mca/ptl/base/ptl_base_header.h"
#include "ptl_gm.h"
#include "ptl_gm_peer.h"
#include "ptl_gm_proc.h"
#include "ptl_gm_sendfrag.h"
#include "ptl_gm_priv.h"
#include "mca/pml/teg/pml_teg_proc.h"

static int mca_ptl_gm_send_quick_fin_message( struct mca_ptl_gm_peer_t* ptl_peer,
					      struct mca_ptl_base_frag_t* frag );

static void mca_ptl_gm_basic_frag_callback( struct gm_port* port, void* context, gm_status_t status )
{
    mca_ptl_gm_module_t* gm_ptl;
    mca_ptl_base_frag_t* frag_base;
    mca_ptl_base_header_t* header;

    header = (mca_ptl_base_header_t*)context;

    frag_base = (mca_ptl_base_frag_t*)header->hdr_frag.hdr_src_ptr.pval;
    gm_ptl = (mca_ptl_gm_module_t *)frag_base->frag_owner;

    switch( status ) {
    case GM_SUCCESS:
	OMPI_GM_FREE_LIST_RETURN( &(gm_ptl->gm_send_dma_frags), ((opal_list_item_t*)header) );
	/* release the send token */
	opal_atomic_add( &(gm_ptl->num_send_tokens), 1 );
        break;
    case GM_SEND_TIMED_OUT:
        opal_output( 0, "send_continue timed out\n" );
        break;
    case GM_SEND_DROPPED:
        opal_output( 0, "send_continue dropped\n" );
        break;
    default:
        opal_output( 0, "send_continue other error %d\n", status );
    }
}

#define DO_DEBUG( INST )

#if OMPI_MCA_PTL_GM_HAVE_RDMA_GET
static inline
int mca_ptl_gm_receiver_advance_pipeline( mca_ptl_gm_recv_frag_t* frag, int onlyifget );

/* This function get called when the gm_get is finish (i.e. when the read from remote memory
 * is completed. We have to send back the ack. If the original data was too large for just one
 * fragment it will be split in severals. We have to send back for each of these fragments one
 * ack.
 */
static void mca_ptl_gm_get_callback( struct gm_port *port, void * context, gm_status_t status )
{
    mca_ptl_gm_recv_frag_t* frag = (mca_ptl_gm_recv_frag_t*)context;
    mca_ptl_gm_peer_t* peer = (mca_ptl_gm_peer_t*)frag->frag_recv.frag_base.frag_peer;

    switch( status ) {
    case GM_SUCCESS:
        DO_DEBUG( opal_output( 0, "receiver %d %p get_callback processed %lld validated %lld",
                               orte_process_info.my_name->vpid, frag, frag->frag_bytes_processed, frag->frag_bytes_validated ); )
        /* send an ack message to the sender */
        mca_ptl_gm_send_quick_fin_message( peer, &(frag->frag_recv.frag_base) );
        peer->get_started = false;
        /* mark the memory as being ready to be deregistered */
        frag->pipeline.lines[frag->pipeline.pos_deregister].flags |= PTL_GM_PIPELINE_DEREGISTER;
        mca_ptl_gm_receiver_advance_pipeline( frag, 0 );
        break;
    case GM_SEND_TIMED_OUT:
        opal_output( 0, "mca_ptl_gm_get_callback timed out\n" );
        break;
    case GM_SEND_DROPPED:
        opal_output( 0, "mca_ptl_gm_get_callback dropped\n" );
        break;
    default:
        opal_output( 0, "mca_ptl_gm_get_callback other error %d\n", status );
    }
}

static inline
int mca_ptl_gm_receiver_advance_pipeline( mca_ptl_gm_recv_frag_t* frag, int onlyifget )
{
    mca_ptl_gm_peer_t* peer;
    gm_status_t status;
    mca_ptl_gm_pipeline_line_t *get_line, *reg_line, *dereg_line;
    uint64_t length;
    DO_DEBUG( int count = 0; char buffer[128]; )

    peer = (mca_ptl_gm_peer_t*)frag->frag_recv.frag_base.frag_peer;
    DO_DEBUG( count = sprintf( buffer, " %p", (void*)frag ); )
    /* start the current get */
    get_line = &(frag->pipeline.lines[frag->pipeline.pos_transfert]);
    if( (PTL_GM_PIPELINE_TRANSFERT & get_line->flags) == PTL_GM_PIPELINE_TRANSFERT ) {
        peer->get_started = true;
        gm_get( peer->peer_ptl->gm_port, get_line->remote_memory.lval,
                get_line->local_memory.pval, get_line->length,
                GM_LOW_PRIORITY, peer->peer_addr.local_id, peer->peer_addr.port_id,
		mca_ptl_gm_get_callback, frag );
        get_line->flags ^= PTL_GM_PIPELINE_REMOTE;
        DO_DEBUG( count += sprintf( buffer + count, " start get %lld (%d)", get_line->length, frag->pipeline.pos_transfert ); );
        frag->pipeline.pos_transfert = (frag->pipeline.pos_transfert + 1) % GM_PIPELINE_DEPTH;
    } else if( 1 == onlyifget ) goto check_completion_status;

    /* register the next segment */
    reg_line = &(frag->pipeline.lines[frag->pipeline.pos_register]);
    length = frag->frag_recv.frag_base.frag_size - frag->frag_bytes_processed;
    if( (0 != length) && !(reg_line->flags & PTL_GM_PIPELINE_REGISTER) ) {
        reg_line->hdr_flags = get_line->hdr_flags;
        reg_line->offset = get_line->offset + get_line->length;
        reg_line->length = length;
        if( reg_line->length > mca_ptl_gm_component.gm_rdma_frag_size )
            reg_line->length = mca_ptl_gm_component.gm_rdma_frag_size;
        reg_line->local_memory.lval = 0L;
        reg_line->local_memory.pval = (char*)frag->frag_recv.frag_base.frag_addr +
                                      reg_line->offset;
        status = mca_ptl_gm_register_memory( peer->peer_ptl->gm_port, reg_line->local_memory.pval,
                                             reg_line->length );
        if( GM_SUCCESS != status ) {
            opal_output( 0, "Cannot register receiver memory (%p, %ld) bytes offset %ld\n",
                         reg_line->local_memory.pval, reg_line->length, reg_line->offset );
            return OMPI_ERROR;
        }
        DO_DEBUG( count += sprintf( buffer + count, " start register %lld offset %lld processed %lld(%d)",
                                    reg_line->length, reg_line->offset, frag->frag_bytes_processed,
                                    frag->pipeline.pos_register ); );
        reg_line->flags |= PTL_GM_PIPELINE_REGISTER;
        frag->frag_bytes_processed += reg_line->length;
        frag->pipeline.pos_register = (frag->pipeline.pos_register + 1) % GM_PIPELINE_DEPTH;
    }

    /* deregister the previous one */
    dereg_line = &(frag->pipeline.lines[frag->pipeline.pos_deregister]);
    if( dereg_line->flags & PTL_GM_PIPELINE_DEREGISTER ) {  /* something usefull */
        status = mca_ptl_gm_deregister_memory( peer->peer_ptl->gm_port,
                                               dereg_line->local_memory.pval, dereg_line->length );
        if( GM_SUCCESS != status ) {
            opal_output( 0, "unpinning receiver memory from get (%p, %u) failed \n",
                         dereg_line->local_memory.pval,
                         dereg_line->length );
        }
        dereg_line->flags ^= (PTL_GM_PIPELINE_DEREGISTER|PTL_GM_PIPELINE_REGISTER);
        assert( dereg_line->flags == 0 );
        frag->frag_bytes_validated += dereg_line->length;
        DO_DEBUG( count += sprintf( buffer + count, " start deregister %lld offset %lld (%d)", dereg_line->length,
                                    dereg_line->offset, frag->pipeline.pos_deregister ); )
        frag->pipeline.pos_deregister = (frag->pipeline.pos_deregister + 1) % GM_PIPELINE_DEPTH;
    }
 check_completion_status:
    if( frag->frag_recv.frag_base.frag_size <= frag->frag_bytes_validated ) {
        peer->peer_ptl->super.ptl_recv_progress( (mca_ptl_base_module_t*)peer->peer_ptl,
						 frag->frag_recv.frag_request, frag->frag_recv.frag_base.frag_size,
						 frag->frag_recv.frag_base.frag_size );
        OMPI_FREE_LIST_RETURN( &(peer->peer_ptl->gm_recv_frags_free), (opal_list_item_t*)frag );
        DO_DEBUG( count += sprintf( buffer + count, " finish" ); )
    }
    DO_DEBUG( opal_output( 0, "receiver %d %s", orte_process_info.my_name->vpid, buffer ); )
    return OMPI_SUCCESS;
}

static inline
int mca_ptl_gm_sender_advance_pipeline( mca_ptl_gm_send_frag_t* frag )
{
    mca_ptl_gm_peer_t* peer;
    gm_status_t status;
    mca_ptl_gm_pipeline_line_t *send_line, *reg_line, *dereg_line;
    mca_ptl_gm_frag_header_t* hdr;
    DO_DEBUG( int count = 0; char buffer[256]; )

    peer = (mca_ptl_gm_peer_t*)frag->frag_send.frag_base.frag_peer;
    DO_DEBUG( count = sprintf( buffer, " %p", (void*)frag ); )
    /* send current segment */
    send_line = &(frag->pipeline.lines[frag->pipeline.pos_transfert]);
    if( (send_line->flags & PTL_GM_PIPELINE_TRANSFERT) == PTL_GM_PIPELINE_TRANSFERT ) {
        opal_list_item_t* item;
        int32_t rc;

        OMPI_FREE_LIST_WAIT( &(peer->peer_ptl->gm_send_dma_frags), item, rc );
        opal_atomic_sub( &(peer->peer_ptl->num_send_tokens), 1 );
        hdr = (mca_ptl_gm_frag_header_t*)item;

        hdr->hdr_frag.hdr_common.hdr_type  = MCA_PTL_HDR_TYPE_FRAG;
        hdr->hdr_frag.hdr_common.hdr_flags = send_line->hdr_flags |
                                             frag->frag_send.frag_base.frag_header.hdr_common.hdr_flags;
        hdr->hdr_frag.hdr_src_ptr.lval     = 0L;  /* for VALGRIND/PURIFY - REPLACE WITH MACRO */
        hdr->hdr_frag.hdr_src_ptr.pval     = frag;
        hdr->hdr_frag.hdr_dst_ptr          = frag->frag_send.frag_base.frag_header.hdr_ack.hdr_dst_match;
        hdr->hdr_frag.hdr_frag_offset      = send_line->offset;
        hdr->hdr_frag.hdr_frag_length      = send_line->length;
        hdr->registered_memory             = send_line->local_memory;

        gm_send_with_callback( peer->peer_ptl->gm_port, hdr,
                               GM_SIZE, sizeof(mca_ptl_gm_frag_header_t),
                               GM_HIGH_PRIORITY, peer->peer_addr.local_id, peer->peer_addr.port_id,
                               mca_ptl_gm_basic_frag_callback, (void*)hdr );

        send_line->flags ^= PTL_GM_PIPELINE_REMOTE;
        frag->pipeline.pos_transfert = (frag->pipeline.pos_transfert + 1) % GM_PIPELINE_DEPTH;
        DO_DEBUG( count += sprintf( buffer + count, " send new fragment %lld", send_line->length ); )
    }

    /* deregister previous segment */
    dereg_line = &(frag->pipeline.lines[frag->pipeline.pos_deregister]);
    if( dereg_line->flags & PTL_GM_PIPELINE_DEREGISTER ) {  /* something usefull */
        status = mca_ptl_gm_deregister_memory( peer->peer_ptl->gm_port,
                                               dereg_line->local_memory.pval, dereg_line->length );
        if( GM_SUCCESS != status ) {
            opal_output( 0, "unpinning receiver memory from get (%p, %u) failed \n",
                         dereg_line->local_memory.pval, dereg_line->length );
        }
        dereg_line->flags ^= (PTL_GM_PIPELINE_REGISTER | PTL_GM_PIPELINE_DEREGISTER);
        assert( dereg_line->flags == 0 );
        frag->frag_bytes_validated += dereg_line->length;
        frag->pipeline.pos_deregister = (frag->pipeline.pos_deregister + 1) % GM_PIPELINE_DEPTH;
        DO_DEBUG( count += sprintf( buffer + count, " start deregister %lld offset %lld (validated %lld)",
                                    dereg_line->length, dereg_line->offset, frag->frag_bytes_validated ); )
    }

    /* register next segment */
    reg_line = &(frag->pipeline.lines[frag->pipeline.pos_register]);
    if( !(reg_line->flags & PTL_GM_PIPELINE_REGISTER) ) {
        reg_line->length = frag->frag_send.frag_base.frag_size - frag->frag_bytes_processed;
        if( 0 != reg_line->length ) {
            reg_line->hdr_flags = frag->frag_send.frag_base.frag_header.hdr_common.hdr_flags;
            if( reg_line->length > mca_ptl_gm_component.gm_rdma_frag_size ) {
                reg_line->length = mca_ptl_gm_component.gm_rdma_frag_size;
            } else {
                reg_line->hdr_flags |= PTL_FLAG_GM_LAST_FRAGMENT;
            }
            reg_line->offset = send_line->offset + send_line->length;
            reg_line->local_memory.lval = 0L;
            reg_line->local_memory.pval = (char*)frag->frag_send.frag_base.frag_addr +
            reg_line->offset;
            status = mca_ptl_gm_register_memory( peer->peer_ptl->gm_port, reg_line->local_memory.pval,
                                                 reg_line->length );
            if( GM_SUCCESS != status ) {
                opal_output( 0, "Cannot register sender memory (%p, %ld) bytes offset %ld\n",
                             reg_line->local_memory.pval, reg_line->length, reg_line->offset );
                return OMPI_ERROR;
            }
            reg_line->flags |= PTL_GM_PIPELINE_TRANSFERT;
            frag->frag_bytes_processed += reg_line->length;
            frag->pipeline.pos_register = (frag->pipeline.pos_register + 1) % GM_PIPELINE_DEPTH;
            DO_DEBUG( count += sprintf( buffer + count, " start register %lld offset %lld",
                                        reg_line->length, reg_line->offset ); )
        }
    }

    DO_DEBUG( opal_output( 0, "sender %d %s", orte_process_info.my_name->vpid, buffer ); )
    return OMPI_SUCCESS;
}
#endif  /* OMPI_MCA_PTL_GM_HAVE_RDMA_GET */

static inline
int mca_ptl_gm_send_internal_rndv_header( mca_ptl_gm_peer_t *ptl_peer,
                                          mca_ptl_gm_send_frag_t *fragment,
                                          mca_ptl_gm_frag_header_t* hdr,
                                          int flags )
{
    struct iovec iov;
    uint32_t in_size;
    size_t max_data;
    int32_t freeAfter;
    ompi_convertor_t *convertor = &(fragment->frag_send.frag_base.frag_convertor);

    iov.iov_base = (char*)hdr + sizeof(mca_ptl_gm_frag_header_t);
    iov.iov_len = fragment->frag_send.frag_base.frag_size - fragment->frag_bytes_processed;
    if( iov.iov_len > (mca_ptl_gm_component.gm_segment_size - sizeof(mca_ptl_gm_frag_header_t))  )
        iov.iov_len = (mca_ptl_gm_component.gm_segment_size - sizeof(mca_ptl_gm_frag_header_t));
    max_data = iov.iov_len;
    in_size = 1;

    if( ompi_convertor_pack(convertor, &(iov), &in_size, &max_data, &freeAfter) < 0)
        return OMPI_ERROR;

    hdr->hdr_frag.hdr_common.hdr_type  = MCA_PTL_HDR_TYPE_FRAG;
    hdr->hdr_frag.hdr_common.hdr_flags = flags;
    hdr->hdr_frag.hdr_src_ptr.lval     = 0L;  /* for VALGRIND/PURIFY - REPLACE WITH MACRO */
    hdr->hdr_frag.hdr_src_ptr.pval     = fragment;
    hdr->hdr_frag.hdr_dst_ptr          = fragment->frag_send.frag_request->req_peer_match;
    hdr->hdr_frag.hdr_frag_offset      = fragment->frag_offset + fragment->frag_bytes_processed;
    hdr->hdr_frag.hdr_frag_length      = fragment->frag_send.frag_base.frag_size -
                                         fragment->frag_bytes_processed;
    hdr->registered_memory.lval        = 0L;
    hdr->registered_memory.pval        = NULL;

    DO_DEBUG( opal_output( 0, "sender %d before send internal rndv header hdr_offset %lld hdr_length %lld max_data %u",
                           orte_process_info.my_name->vpid, hdr->hdr_frag.hdr_frag_offset, hdr->hdr_frag.hdr_frag_length, max_data ); );
    gm_send_with_callback( ptl_peer->peer_ptl->gm_port, hdr, GM_SIZE,
                           sizeof(mca_ptl_gm_frag_header_t) + max_data,
                           GM_LOW_PRIORITY, ptl_peer->peer_addr.local_id, ptl_peer->peer_addr.port_id,
                           mca_ptl_gm_basic_frag_callback, (void *)hdr );
    fragment->frag_bytes_processed += max_data;
    fragment->frag_bytes_validated += max_data;
    DO_DEBUG( opal_output( 0, "sender %d after send internal rndv header processed %lld, validated %lld max_data %u",
                           orte_process_info.my_name->vpid, fragment->frag_bytes_processed, fragment->frag_bytes_validated, max_data ); );
    return OMPI_SUCCESS;
}

static inline
int mca_ptl_gm_send_burst_data( mca_ptl_gm_peer_t *ptl_peer,
                                mca_ptl_gm_send_frag_t *fragment,
                                uint32_t burst_length,
                                mca_ptl_base_frag_header_t* hdr,
                                int32_t flags )
{
    int32_t freeAfter, rc;
    uint32_t in_size;
    size_t max_data;
    struct iovec iov;
    ompi_convertor_t *convertor = &(fragment->frag_send.frag_base.frag_convertor);

    while( 0 < burst_length ) {  /* send everything for the burst_length size */
        if( NULL == hdr ) {
            opal_list_item_t* item;
            OMPI_FREE_LIST_WAIT( &(ptl_peer->peer_ptl->gm_send_dma_frags), item, rc );
            opal_atomic_sub( &(ptl_peer->peer_ptl->num_send_tokens), 1 );
            hdr = (mca_ptl_base_frag_header_t*)item;
        }
        iov.iov_base = (char*)hdr + sizeof(mca_ptl_base_frag_header_t);
        iov.iov_len = mca_ptl_gm_component.gm_segment_size - sizeof(mca_ptl_base_frag_header_t);
        if( iov.iov_len >= burst_length )
            iov.iov_len = burst_length;
        max_data = iov.iov_len;
        in_size = 1;

        if( ompi_convertor_pack(convertor, &(iov), &in_size, &max_data, &freeAfter) < 0)
            return OMPI_ERROR;

        hdr->hdr_common.hdr_type  = MCA_PTL_HDR_TYPE_FRAG;
        hdr->hdr_common.hdr_flags = flags;
        hdr->hdr_src_ptr.lval     = 0L;  /* for VALGRIND/PURIFY - REPLACE WITH MACRO */
        hdr->hdr_src_ptr.pval     = fragment;
        hdr->hdr_dst_ptr          = fragment->frag_send.frag_request->req_peer_match;
        assert( hdr->hdr_dst_ptr.pval != NULL );
        hdr->hdr_frag_offset      = fragment->frag_offset + fragment->frag_bytes_processed;
        hdr->hdr_frag_length      = max_data;

        fragment->frag_bytes_processed += max_data;
        fragment->frag_bytes_validated += max_data;
        burst_length -= max_data;
        if( fragment->frag_send.frag_base.frag_size == fragment->frag_bytes_processed ) {
            assert( 0 == burst_length );
            hdr->hdr_common.hdr_flags |= PTL_FLAG_GM_LAST_FRAGMENT;
        }
        /* for the last piece set the header type to FIN */
        gm_send_with_callback( ptl_peer->peer_ptl->gm_port, hdr, GM_SIZE,
                               iov.iov_len + sizeof(mca_ptl_base_frag_header_t),
                               GM_LOW_PRIORITY, ptl_peer->peer_addr.local_id, ptl_peer->peer_addr.port_id,
                               mca_ptl_gm_basic_frag_callback, (void*)hdr );
        hdr = NULL;  /* force to retrieve a new one on the next loop */
    }
    DO_DEBUG( opal_output( 0, "sender %d after burst offset %lld, processed %lld, validated %lld\n",
                           orte_process_info.my_name->vpid, fragment->frag_offset, fragment->frag_bytes_processed, fragment->frag_bytes_validated); );
    return OMPI_SUCCESS;
}

int mca_ptl_gm_peer_send_continue( mca_ptl_gm_peer_t *ptl_peer,
                                   mca_ptl_gm_send_frag_t *fragment,
                                   struct mca_ptl_base_send_request_t *sendreq,
                                   size_t offset,
                                   size_t *size,
                                   int flags )
{
    mca_ptl_gm_frag_header_t* hdr;
    uint64_t remaining_bytes, burst_length;
    opal_list_item_t *item;
    int rc = 0;
#if OMPI_MCA_PTL_GM_HAVE_RDMA_GET
    gm_status_t status;
    mca_ptl_gm_pipeline_line_t* pipeline;
#endif  /* OMPI_MCA_PTL_GM_HAVE_RDMA_GET */

    fragment->frag_offset = offset;

    /* must update the offset after actual fragment size is determined
     * before attempting to send the fragment
     */
    mca_ptl_base_send_request_offset( fragment->frag_send.frag_request,
                                      fragment->frag_send.frag_base.frag_size );
    DO_DEBUG( opal_output( 0, "sender %d start new send length %ld offset %ld\n", orte_process_info.my_name->vpid, *size, offset ); )
    /* The first DMA memory buffer has been alocated in same time as the fragment */
    item = (opal_list_item_t*)fragment->send_buf;
    hdr = (mca_ptl_gm_frag_header_t*)item;
    remaining_bytes = fragment->frag_send.frag_base.frag_size - fragment->frag_bytes_processed;
    if( remaining_bytes < mca_ptl_gm_component.gm_eager_limit ) {
        burst_length = remaining_bytes;
    } else {
#if OMPI_MCA_PTL_GM_HAVE_RDMA_GET
        if( remaining_bytes < mca_ptl_gm_component.gm_rndv_burst_limit ) {
            burst_length = remaining_bytes % (mca_ptl_gm_component.gm_segment_size - sizeof(mca_ptl_base_frag_header_t));
        } else {
            if( mca_ptl_gm_component.gm_rdma_frag_size == UINT_MAX )
                burst_length = 0;
            else
                burst_length = remaining_bytes % mca_ptl_gm_component.gm_rdma_frag_size;
        }
#else
        /*burst_length = remaining_bytes % (mca_ptl_gm_component.gm_segment_size - sizeof(mca_ptl_base_frag_header_t));*/
        burst_length = (mca_ptl_gm_component.gm_segment_size - sizeof(mca_ptl_base_frag_header_t));
#endif  /* OMPI_MCA_PTL_GM_HAVE_RDMA_GET */
    }

    if( burst_length > 0 ) {
        mca_ptl_gm_send_burst_data( ptl_peer, fragment, burst_length, &(hdr->hdr_frag), flags );
        item = NULL;  /* this buffer was already used by the mca_ptl_gm_send_burst_data function */
        DO_DEBUG( opal_output( 0, "sender %d burst %ld bytes", orte_process_info.my_name->vpid, burst_length ); );
    }

    if( fragment->frag_send.frag_base.frag_size == fragment->frag_bytes_processed ) {
        *size = fragment->frag_bytes_processed;
        if( !(flags & MCA_PTL_FLAGS_ACK) ) {
            ptl_peer->peer_ptl->super.ptl_send_progress( (mca_ptl_base_module_t*)ptl_peer->peer_ptl,
                                                         fragment->frag_send.frag_request,
                                                         (*size) );
            OMPI_FREE_LIST_RETURN( &(ptl_peer->peer_ptl->gm_send_frags), ((opal_list_item_t*)fragment) );
        }
        return OMPI_SUCCESS;
    }
    if( NULL == item ) {
        OMPI_FREE_LIST_WAIT( &(ptl_peer->peer_ptl->gm_send_dma_frags), item, rc );
        opal_atomic_sub( &(ptl_peer->peer_ptl->num_send_tokens), 1 );
        hdr = (mca_ptl_gm_frag_header_t*)item;
    }

    /* Large set of data => we have to setup a rendez-vous protocol. Here we can
     * use the match header already filled in by the upper level and just complete it
     * with the others informations. When we reach this point the rendez-vous protocol
     * has already been realized so we know that the receiver expect our message.
     */
#if OMPI_MCA_PTL_GM_HAVE_RDMA_GET
    /* Trigger the long rendez-vous protocol only if gm_get is supported */
    if( remaining_bytes > mca_ptl_gm_component.gm_rndv_burst_limit )
        flags |= PTL_FLAG_GM_REQUIRE_LOCK;
#endif  /* OMPI_MCA_PTL_GM_HAVE_RDMA_GET */
    mca_ptl_gm_send_internal_rndv_header( ptl_peer, fragment, hdr, flags );
    if( !(PTL_FLAG_GM_REQUIRE_LOCK & flags) )
        return OMPI_SUCCESS;

#if OMPI_MCA_PTL_GM_HAVE_RDMA_GET
    pipeline = &(fragment->pipeline.lines[0]);
    pipeline->length = fragment->frag_send.frag_base.frag_size - fragment->frag_bytes_processed;
    if( pipeline->length > mca_ptl_gm_component.gm_rdma_frag_size ) {
        pipeline->length = mca_ptl_gm_component.gm_rdma_frag_size;
    }
    pipeline->offset = fragment->frag_offset + fragment->frag_bytes_processed;
    pipeline->hdr_flags = fragment->frag_send.frag_base.frag_header.hdr_common.hdr_flags;
    pipeline->local_memory.lval = 0L;
    pipeline->local_memory.pval = (char*)fragment->frag_send.frag_base.frag_addr + pipeline->offset;
    status = mca_ptl_gm_register_memory( ptl_peer->peer_ptl->gm_port, pipeline->local_memory.pval,
                                         pipeline->length );
    if( GM_SUCCESS != status ) {
        opal_output( 0, "Cannot register sender memory (%p, %ld) bytes offset %ld\n",
                     pipeline->local_memory.pval, pipeline->length, pipeline->offset );
    }
    pipeline->flags  = PTL_GM_PIPELINE_TRANSFERT;
    fragment->frag_bytes_processed += pipeline->length;
    DO_DEBUG( opal_output( 0, "sender %d %p start register %lld (%d)", orte_process_info.my_name->vpid,
                           fragment, pipeline->length, fragment->pipeline.pos_register ); )
    fragment->pipeline.pos_register = (fragment->pipeline.pos_register + 1) % GM_PIPELINE_DEPTH;
    /* Now we are waiting for the ack message. Meanwhile we can register the sender first piece
     * of data. In this way we have a recovery between the expensive registration on both sides.
     */
#else
    assert( 0 );
#endif  /* OMPI_MCA_PTL_GM_HAVE_RDMA_GET */
    return OMPI_SUCCESS;
}

static void send_match_callback( struct gm_port* port, void* context, gm_status_t status )
{
    mca_ptl_gm_module_t* gm_ptl;
    mca_ptl_base_header_t* header = (mca_ptl_base_header_t*)context;

    gm_ptl = (mca_ptl_gm_module_t*)((long)header->hdr_rndv.hdr_frag_length);

    OMPI_GM_FREE_LIST_RETURN( &(gm_ptl->gm_send_dma_frags), ((opal_list_item_t*)header) );
    /* release the send token */
    opal_atomic_add( &(gm_ptl->num_send_tokens), 1 );
}

/* This function is used for the initial send. For small size messages the data will be attached
 * to the header, when for long size messages we will setup a rendez-vous protocol. We dont need
 * to fill a fragment description here as all that we need is the request pointer. In same time
 * even if we fill a fragment it will be lost as soon as we get the answer from the remote node
 * and we will be unable to reuse any informations stored inside (like the convertor).
 */
int mca_ptl_gm_peer_send( struct mca_ptl_base_module_t* ptl,
                          struct mca_ptl_base_peer_t* ptl_base_peer,
                          struct mca_ptl_base_send_request_t *sendreq,
                          size_t offset,
                          size_t size,
                          int flags )
{
    const int header_length = sizeof(mca_ptl_base_rendezvous_header_t);
    mca_ptl_base_header_t* hdr;
    mca_ptl_gm_module_t* ptl_gm = (mca_ptl_gm_module_t*)ptl;
    ompi_convertor_t *convertor = NULL;
    int rc, freeAfter;
    size_t max_data = 0;
    mca_ptl_gm_peer_t* ptl_peer = (mca_ptl_gm_peer_t*)ptl_base_peer;
    opal_list_item_t *item;
    char* sendbuf;

    OMPI_FREE_LIST_WAIT( &(ptl_gm->gm_send_dma_frags), item, rc );
    opal_atomic_sub( &(ptl_gm->num_send_tokens), 1 );
    sendbuf = (char*)item;

    hdr = (mca_ptl_base_header_t*)item;

    /* Populate the header with the match informations */
    (void)mca_ptl_gm_init_header_rndv( hdr, sendreq, flags );
    hdr->hdr_rndv.hdr_frag_length = (uint64_t)((long)ptl);

    if( size > 0 ) {
        struct iovec iov;
        uint32_t iov_count = 1;

        convertor = &sendreq->req_send.req_convertor;
        /* We send here the first fragment, and the convertor does not need any
         * particular options. Thus, we can use the one already prepared on the
         * request.
         */

        if( (size + header_length) <= mca_ptl_gm_component.gm_segment_size )
            iov.iov_len = size;
        else
            iov.iov_len = mca_ptl_gm_component.gm_segment_size - header_length;

        /* copy the data to the registered buffer */
        iov.iov_base = ((char*)hdr) + header_length;
        max_data = iov.iov_len;
        if((rc = ompi_convertor_pack(convertor, &(iov), &iov_count, &max_data, &freeAfter)) < 0)
            return OMPI_ERROR;

        assert( max_data != 0 );
        /* must update the offset after actual fragment size is determined
         * before attempting to send the fragment
         */
        mca_ptl_base_send_request_offset( sendreq, max_data );
    }
    /* Send the first fragment */
    gm_send_with_callback( ptl_gm->gm_port, hdr,
                           GM_SIZE, max_data + header_length, GM_LOW_PRIORITY,
                           ptl_peer->peer_addr.local_id, ptl_peer->peer_addr.port_id,
                           send_match_callback, (void *)hdr );

    if( !(flags & MCA_PTL_FLAGS_ACK) ) {
        ptl->ptl_send_progress( ptl, sendreq, max_data );
        DO_DEBUG( opal_output( 0, "sender %d complete request %p w/o rndv with %d bytes",
                               orte_process_info.my_name->vpid, sendreq, max_data ); );
    } else {
        DO_DEBUG( opal_output( 0, "sender %d sent request %p for rndv with %d bytes",
                               orte_process_info.my_name->vpid, sendreq, max_data ); );
    }

    return OMPI_SUCCESS;
}

static mca_ptl_gm_recv_frag_t*
mca_ptl_gm_recv_frag_ctrl( struct mca_ptl_gm_module_t *ptl,
                           mca_ptl_base_header_t * header, uint32_t msg_len )
{
    mca_ptl_base_send_request_t *req;

    assert( MCA_PTL_FLAGS_ACK & header->hdr_common.hdr_flags );
    req = (mca_ptl_base_send_request_t*)(header->hdr_ack.hdr_src_ptr.pval);
    req->req_peer_match = header->hdr_ack.hdr_dst_match;
    req->req_peer_addr  = header->hdr_ack.hdr_dst_addr;
    req->req_peer_size  = header->hdr_ack.hdr_dst_size;
    DO_DEBUG( opal_output( 0, "sender %d get back the rendez-vous for request %p",
                           orte_process_info.my_name->vpid, req ); );
    ptl->super.ptl_send_progress( (mca_ptl_base_module_t*)ptl, req, req->req_offset );

    return NULL;
}

/* We get a RNDV header in two situations:
 * - when the remote node need a ack
 * - when we set a rendez-vous protocol with the remote node.
 * In both cases we have to send an ack back.
 */
static mca_ptl_gm_recv_frag_t*
mca_ptl_gm_recv_frag_match( struct mca_ptl_gm_module_t *ptl,
                            mca_ptl_base_header_t* hdr, uint32_t msg_len )
{
    mca_ptl_gm_recv_frag_t* recv_frag;
    bool matched;

    /* allocate a receive fragment */
    recv_frag = mca_ptl_gm_alloc_recv_frag( (struct mca_ptl_base_module_t*)ptl );

    if( MCA_PTL_HDR_TYPE_MATCH == hdr->hdr_rndv.hdr_match.hdr_common.hdr_type ) {
        recv_frag->frag_recv.frag_base.frag_addr =
            (char*)hdr + sizeof(mca_ptl_base_match_header_t);
        recv_frag->frag_recv.frag_base.frag_size = hdr->hdr_match.hdr_msg_length;
    } else {
        assert( MCA_PTL_HDR_TYPE_RNDV == hdr->hdr_rndv.hdr_match.hdr_common.hdr_type );
        recv_frag->frag_recv.frag_base.frag_addr =
            (char*)hdr + sizeof(mca_ptl_base_rendezvous_header_t);
        recv_frag->frag_recv.frag_base.frag_size = hdr->hdr_rndv.hdr_match.hdr_msg_length;
    }
    recv_frag->frag_recv.frag_is_buffered    = false;
    recv_frag->have_allocated_buffer         = false;
    recv_frag->attached_data_length          = msg_len - sizeof(mca_ptl_base_rendezvous_header_t);
    recv_frag->frag_recv.frag_base.frag_peer = NULL;
    recv_frag->frag_recv.frag_base.frag_header.hdr_rndv = hdr->hdr_rndv;
    matched = ptl->super.ptl_match( &(ptl->super),
                                    &(recv_frag->frag_recv),
                                    &(recv_frag->frag_recv.frag_base.frag_header.hdr_match) );
    if( true == matched ) return NULL;  /* done and fragment already removed */

    /* get some memory and copy the data inside. We can then release the receive buffer */
    if( 0 != recv_frag->attached_data_length ) {
        char* ptr = (char*)mca_ptl_gm_get_local_buffer();
        recv_frag->have_allocated_buffer = true;
        memcpy( ptr, recv_frag->frag_recv.frag_base.frag_addr, recv_frag->attached_data_length );
        recv_frag->frag_recv.frag_base.frag_addr = ptr;
    } else {
        recv_frag->frag_recv.frag_base.frag_addr = NULL;
    }
    recv_frag->matched = false;

    return recv_frag;
}

static void recv_short_callback( struct gm_port* port, void* context, gm_status_t status )
{
    mca_ptl_gm_module_t* gm_ptl;
    mca_ptl_base_frag_t* frag_base;
    mca_ptl_base_ack_header_t* header;

    header = (mca_ptl_base_ack_header_t*)context;

    frag_base = (mca_ptl_base_frag_t*)header->hdr_dst_match.pval;
    gm_ptl = (mca_ptl_gm_module_t *)frag_base->frag_owner;

    OMPI_GM_FREE_LIST_RETURN( &(gm_ptl->gm_send_dma_frags), ((opal_list_item_t*)header) );
    /* release the send token */
    opal_atomic_add( &(gm_ptl->num_send_tokens), 1 );
}

static int mca_ptl_gm_send_quick_fin_message( struct mca_ptl_gm_peer_t* ptl_peer,
					      struct mca_ptl_base_frag_t* frag )
{
    opal_list_item_t *item;
    mca_ptl_base_header_t *hdr;
    int rc;

    OMPI_FREE_LIST_WAIT( &(ptl_peer->peer_ptl->gm_send_dma_frags), item, rc );
    opal_atomic_sub( &(ptl_peer->peer_ptl->num_send_tokens), 1 );
    hdr = (mca_ptl_base_header_t*)item;

    hdr->hdr_common.hdr_type        = MCA_PTL_HDR_TYPE_FIN;
    hdr->hdr_common.hdr_flags       = PTL_FLAG_GM_HAS_FRAGMENT | frag->frag_header.hdr_common.hdr_flags;
    hdr->hdr_ack.hdr_src_ptr.pval   = frag->frag_header.hdr_frag.hdr_src_ptr.pval;
    hdr->hdr_ack.hdr_dst_match.lval = 0;
    hdr->hdr_ack.hdr_dst_match.pval = frag;
    hdr->hdr_ack.hdr_dst_addr.lval  = 0; /*we are filling both p and val of dest address */
    hdr->hdr_ack.hdr_dst_addr.pval  = NULL;
    hdr->hdr_ack.hdr_dst_size       = frag->frag_header.hdr_frag.hdr_frag_length;

    gm_send_with_callback(ptl_peer->peer_ptl->gm_port, hdr,
                          GM_SIZE, sizeof(mca_ptl_base_ack_header_t),
                          GM_HIGH_PRIORITY, ptl_peer->peer_addr.local_id, ptl_peer->peer_addr.port_id,
                          recv_short_callback, (void*)hdr );
    DO_DEBUG( opal_output( 0, "receiver %d %p send quick message for length %lld", orte_process_info.my_name->vpid,
                           frag, frag->frag_header.hdr_frag.hdr_frag_length ); )
    return OMPI_SUCCESS;
}

static mca_ptl_gm_recv_frag_t*
mca_ptl_gm_recv_frag_frag( struct mca_ptl_gm_module_t* ptl,
                           mca_ptl_gm_frag_header_t* hdr, uint32_t msg_len )
{
    mca_ptl_base_recv_request_t *request;
    ompi_convertor_t local_convertor, *convertor;
    struct iovec iov;
    uint32_t iov_count, header_length;
    size_t max_data = 0;
    int32_t freeAfter, rc;
    mca_ptl_gm_recv_frag_t* frag;

    header_length = sizeof(mca_ptl_base_frag_header_t);
    if( hdr->hdr_frag.hdr_common.hdr_flags & PTL_FLAG_GM_HAS_FRAGMENT ) {
        frag = (mca_ptl_gm_recv_frag_t*)hdr->hdr_frag.hdr_dst_ptr.pval;
        frag->frag_recv.frag_base.frag_header.hdr_frag = hdr->hdr_frag;
        request = (mca_ptl_base_recv_request_t*)frag->frag_recv.frag_request;
        /* here we can have a synchronisation problem if several threads work in same time
         * with the same request. The only question is if it's possible ?
         */
        convertor = &(frag->frag_recv.frag_base.frag_convertor);
        DO_DEBUG( opal_output( 0, "receiver %d get message tagged as HAS_FRAGMENT", orte_process_info.my_name->vpid ); );
        if( PTL_FLAG_GM_REQUIRE_LOCK & hdr->hdr_frag.hdr_common.hdr_flags )
            header_length = sizeof(mca_ptl_gm_frag_header_t);
    } else {
        request = (mca_ptl_base_recv_request_t*)hdr->hdr_frag.hdr_dst_ptr.pval;

        if( hdr->hdr_frag.hdr_frag_length <= (mca_ptl_gm_component.gm_segment_size -
                                              sizeof(mca_ptl_base_frag_header_t)) ) {
            convertor = &local_convertor;
            request->req_recv.req_base.req_proc = 
                ompi_comm_peer_lookup( request->req_recv.req_base.req_comm,
                                       request->req_recv.req_base.req_ompi.req_status.MPI_SOURCE );
            frag = NULL;
        } else {  /* large message => we have to create a receive fragment */
            frag = mca_ptl_gm_alloc_recv_frag( (struct mca_ptl_base_module_t*)ptl );
            frag->frag_recv.frag_request = request;
            frag->frag_offset = hdr->hdr_frag.hdr_frag_offset;
            frag->matched = true;
            frag->frag_recv.frag_base.frag_addr = frag->frag_recv.frag_request->req_recv.req_base.req_addr;
            frag->frag_recv.frag_base.frag_size = hdr->hdr_frag.hdr_frag_length;
            frag->frag_recv.frag_base.frag_peer = (struct mca_ptl_base_peer_t*)
                mca_pml_teg_proc_lookup_remote_peer( request->req_recv.req_base.req_comm,
                                                     request->req_recv.req_base.req_ompi.req_status.MPI_SOURCE,
                                                     (struct mca_ptl_base_module_t*)ptl );
            /* send an ack message to the sender ... quick hack (TODO) */
            frag->frag_recv.frag_base.frag_header.hdr_frag = hdr->hdr_frag;
            frag->frag_recv.frag_base.frag_header.hdr_frag.hdr_frag_length = 0;
            mca_ptl_gm_send_quick_fin_message( (mca_ptl_gm_peer_t*)frag->frag_recv.frag_base.frag_peer,
                                               &(frag->frag_recv.frag_base) );
            header_length = sizeof(mca_ptl_gm_frag_header_t);
            frag->frag_recv.frag_base.frag_header.hdr_frag.hdr_frag_length = hdr->hdr_frag.hdr_frag_length;
            convertor = &(frag->frag_recv.frag_base.frag_convertor);
            DO_DEBUG( opal_output( 0, "receiver %d create fragment with offset %lld and length %lld",
                                   orte_process_info.my_name->vpid, frag->frag_offset, frag->frag_recv.frag_base.frag_size ); );
        }
        /* GM does not use any of the convertor specializations, so we can just clone the
         * standard convertor attached to the request and set the position.
         */
        ompi_convertor_clone_with_position( &(request->req_recv.req_convertor),
                                            convertor, 1,
                                            (size_t*)&(hdr->hdr_frag.hdr_frag_offset) );
    }

    if( header_length != msg_len ) {
        iov.iov_base = (char*)hdr + header_length;
        iov.iov_len  = msg_len - header_length;
        iov_count = 1;
        max_data = iov.iov_len;
        freeAfter = 0;  /* unused here */
        rc = ompi_convertor_unpack( convertor, &iov, &iov_count, &max_data, &freeAfter );
        assert( 0 == freeAfter );
        /* If we are in a short burst mode then update the request */
        if( NULL == frag ) {
            ptl->super.ptl_recv_progress( (mca_ptl_base_module_t*)ptl, request, max_data, max_data );
            return NULL;
        }
    }

    /* Update the status of the fragment depending on the amount of data converted so far */
    frag->frag_bytes_processed += max_data;
    frag->frag_bytes_validated += max_data;
    if( !(PTL_FLAG_GM_REQUIRE_LOCK & hdr->hdr_frag.hdr_common.hdr_flags) ) {
        if( frag->frag_bytes_validated == frag->frag_recv.frag_base.frag_size ) {
            ptl->super.ptl_recv_progress( (mca_ptl_base_module_t*)ptl, request,
                                          frag->frag_recv.frag_base.frag_size,
                                          frag->frag_recv.frag_base.frag_size );
            OMPI_FREE_LIST_RETURN( &(((mca_ptl_gm_peer_t*)frag->frag_recv.frag_base.frag_peer)->peer_ptl->gm_recv_frags_free), (opal_list_item_t*)frag );
        }
        DO_DEBUG( opal_output( 0, "receiver %d waiting for burst with fragment ...", orte_process_info.my_name->vpid ); );
        return NULL;
    }

#if OMPI_MCA_PTL_GM_HAVE_RDMA_GET
    {
        mca_ptl_gm_pipeline_line_t* pipeline;

        /* There is a kind of rendez-vous protocol used internally by the GM driver. If the amount of data
         * to transfert is large enough, then the sender will start sending a frag message with the
         * remote_memory set to NULL (but with the length set to the length of the first fragment).
         * It will allow the receiver to start to register it's own memory. Later when the receiver
         * get a fragment with the remote_memory field not NULL it can start getting the data.
         */
        if( NULL == hdr->registered_memory.pval ) {  /* first round of the local rendez-vous protocol */
            pipeline = &(frag->pipeline.lines[0]);
            pipeline->hdr_flags = hdr->hdr_frag.hdr_common.hdr_flags;
            pipeline->offset    = frag->frag_offset + frag->frag_bytes_processed;
            pipeline->length    = 0;  /* we can lie about this one */
            mca_ptl_gm_receiver_advance_pipeline( frag, 0 );
        } else {
            pipeline = &(frag->pipeline.lines[frag->pipeline.pos_remote]);
            DO_DEBUG( opal_output( 0, "receiver %d %p get remote memory length %lld (%d)\n",
                                   orte_process_info.my_name->vpid, frag, hdr->hdr_frag.hdr_frag_length, frag->pipeline.pos_remote ); );
            frag->pipeline.pos_remote = (frag->pipeline.pos_remote + 1) % GM_PIPELINE_DEPTH;
            assert( (pipeline->flags & PTL_GM_PIPELINE_REMOTE) == 0 );
            pipeline->remote_memory = hdr->registered_memory;
            pipeline->flags |= PTL_GM_PIPELINE_REMOTE;
            mca_ptl_gm_receiver_advance_pipeline( frag, 0 );
        }
    }
#else
    assert( 0 );
#endif  /* OMPI_MCA_PTL_GM_HAVE_RDMA_GET */

    return NULL;
}

static mca_ptl_gm_recv_frag_t*
mca_ptl_gm_recv_frag_fin( struct mca_ptl_gm_module_t* ptl,
			  mca_ptl_base_header_t* hdr, uint32_t msg_len )
{
    mca_ptl_gm_send_frag_t* frag;

    frag = (mca_ptl_gm_send_frag_t*)hdr->hdr_ack.hdr_src_ptr.pval;

    frag->frag_send.frag_base.frag_header.hdr_common.hdr_flags = hdr->hdr_common.hdr_flags;
    frag->frag_send.frag_base.frag_header.hdr_ack.hdr_dst_match = hdr->hdr_ack.hdr_dst_match;
    frag->frag_send.frag_request->req_peer_match = hdr->hdr_ack.hdr_dst_match;
    if( PTL_FLAG_GM_REQUIRE_LOCK & hdr->hdr_common.hdr_flags ) {
#if OMPI_MCA_PTL_GM_HAVE_RDMA_GET
        if( 0 == hdr->hdr_ack.hdr_dst_size ) {
            DO_DEBUG( opal_output( 0, "sender %d %p get FIN message (initial)", orte_process_info.my_name->vpid, frag ); );
            /* I just receive the ack for the first fragment => setup the pipeline */
            mca_ptl_gm_sender_advance_pipeline( frag );
        } else {
            /* mark the memory as ready to be deregistered */
            frag->pipeline.lines[frag->pipeline.pos_deregister].flags |= PTL_GM_PIPELINE_DEREGISTER;
            DO_DEBUG( opal_output( 0, "sender %d %p get FIN message (%d)", orte_process_info.my_name->vpid, frag, frag->pipeline.pos_deregister ); );
        }
        /* continue the pipeline ... send the next segment */
        mca_ptl_gm_sender_advance_pipeline( frag );
#else
        assert( 0 );
#endif  /* OMPI_MCA_PTL_GM_HAVE_RDMA_GET */
    } else {
        DO_DEBUG( opal_output( 0, "sender %d burst data after rendez-vous protocol", orte_process_info.my_name->vpid ); );
        /* do a burst but with the remote fragment as we just get it from the message */
        mca_ptl_gm_send_burst_data( (mca_ptl_gm_peer_t*)frag->frag_send.frag_base.frag_peer, frag,
                                    frag->frag_send.frag_base.frag_size - frag->frag_bytes_validated,
                                    NULL, hdr->hdr_common.hdr_flags );
    }
    if( frag->frag_send.frag_base.frag_size == frag->frag_bytes_validated ) {
        DO_DEBUG( opal_output( 0, "sender %d complete send operation", orte_process_info.my_name->vpid ); );
        ptl->super.ptl_send_progress( (mca_ptl_base_module_t*)ptl,
                                      frag->frag_send.frag_request,
                                      frag->frag_bytes_validated );
        OMPI_FREE_LIST_RETURN( &(ptl->gm_send_frags), (opal_list_item_t*)frag );
    }

    return NULL;
}

void mca_ptl_gm_outstanding_recv( struct mca_ptl_gm_module_t *ptl )
{
    mca_ptl_gm_recv_frag_t * frag = NULL;
    int  size;
    bool matched;

    size = opal_list_get_size (&ptl->gm_recv_outstanding_queue);

    if (size > 0) {
        frag = (mca_ptl_gm_recv_frag_t *)
	    opal_list_remove_first( (opal_list_t *)&(ptl->gm_recv_outstanding_queue) );


        matched = ptl->super.ptl_match( &(ptl->super), &(frag->frag_recv),
                                        &(frag->frag_recv.frag_base.frag_header.hdr_match) );

        if(!matched) {
            opal_list_append((opal_list_t *)&(ptl->gm_recv_outstanding_queue),
                             (opal_list_item_t *) frag);
        } else {
            /* if allocated buffer, free the buffer */
            /* return the recv descriptor to the free list */
            OMPI_FREE_LIST_RETURN(&(ptl->gm_recv_frags_free), (opal_list_item_t *)frag);
        }
    }
}

mca_ptl_gm_frag_management_fct_t* mca_ptl_gm_frag_management_fct[MCA_PTL_HDR_TYPE_MAX] = {
    NULL,  /* empty no header type equal to zero */
    NULL,  /* mca_ptl_gm_recv_frag_match, */
    mca_ptl_gm_recv_frag_match,
    (mca_ptl_gm_frag_management_fct_t*)mca_ptl_gm_recv_frag_frag,  /* force the conversion to remove a warning */
    mca_ptl_gm_recv_frag_ctrl,
    NULL,
    NULL,
    mca_ptl_gm_recv_frag_fin,
    NULL };

int mca_ptl_gm_analyze_recv_event( struct mca_ptl_gm_module_t* ptl, gm_recv_event_t* event )
{
    mca_ptl_base_header_t *header = NULL, *release_buf;
    mca_ptl_gm_frag_management_fct_t* function;
    uint32_t priority = GM_HIGH_PRIORITY, msg_len;

    release_buf = (mca_ptl_base_header_t*)gm_ntohp(event->recv.buffer);

    switch (gm_ntohc(event->recv.type)) {
    case GM_FAST_RECV_EVENT:
    case GM_FAST_PEER_RECV_EVENT:
        priority = GM_LOW_PRIORITY;
    case GM_FAST_HIGH_RECV_EVENT:
    case GM_FAST_HIGH_PEER_RECV_EVENT:
        header = (mca_ptl_base_header_t *)gm_ntohp(event->recv.message);
        break;
    case GM_RECV_EVENT:
    case GM_PEER_RECV_EVENT:
        priority = GM_LOW_PRIORITY;
    case GM_HIGH_RECV_EVENT:
    case GM_HIGH_PEER_RECV_EVENT:
        header = release_buf;
        break;
    case GM_NO_RECV_EVENT:
	
    default:
        gm_unknown(ptl->gm_port, event);
        return 1;
    }

    assert( header->hdr_common.hdr_type < MCA_PTL_HDR_TYPE_MAX );
    function = mca_ptl_gm_frag_management_fct[header->hdr_common.hdr_type];
    assert( NULL != function );

    msg_len = gm_ntohl( event->recv.length );
    (void)function( ptl, header, msg_len );

    gm_provide_receive_buffer( ptl->gm_port, release_buf, GM_SIZE, priority );

    return 0;
}


void mca_ptl_gm_dump_header( char* str, mca_ptl_base_header_t* hdr )
{
    switch( hdr->hdr_common.hdr_type ) {
    case MCA_PTL_HDR_TYPE_MATCH:
        goto print_match_hdr;
    case MCA_PTL_HDR_TYPE_RNDV:
        goto print_rndv_hdr;
    case MCA_PTL_HDR_TYPE_FRAG:
        goto print_frag_hdr;
    case MCA_PTL_HDR_TYPE_ACK:
        goto print_ack_hdr;
    case MCA_PTL_HDR_TYPE_NACK:
        goto print_ack_hdr;
    case MCA_PTL_HDR_TYPE_GET:
        goto print_match_hdr;
    case MCA_PTL_HDR_TYPE_FIN:
        goto print_ack_hdr;
    case MCA_PTL_HDR_TYPE_FIN_ACK:
        goto print_match_hdr;
    default:
        opal_output( 0, "unknown header of type %d\n", hdr->hdr_common.hdr_type );
    }
    return;

 print_ack_hdr:
    opal_output( 0, "%s hdr_common hdr_type %d hdr_flags %x\nack header hdr_src_ptr (lval %lld, pval %p)\n           hdr_dst_match (lval %lld pval %p)\n           hdr_dst_addr (lval %lld pval %p)\n           hdr_dst_size %lld\n",
		 str, hdr->hdr_common.hdr_type, hdr->hdr_common.hdr_flags,
		 hdr->hdr_ack.hdr_src_ptr.lval, hdr->hdr_ack.hdr_src_ptr.pval,
		 hdr->hdr_ack.hdr_dst_match.lval, hdr->hdr_ack.hdr_dst_match.pval,
		 hdr->hdr_ack.hdr_dst_addr.lval, hdr->hdr_ack.hdr_dst_addr.pval, hdr->hdr_ack.hdr_dst_size );
    return;
 print_frag_hdr:
    opal_output( 0, "%s hdr_common hdr_type %d hdr_flags %x\nfrag header hdr_frag_length %lld hdr_frag_offset %lld\n            hdr_src_ptr (lval %lld, pval %p)\n            hdr_dst_ptr (lval %lld, pval %p)\n",
		 str, hdr->hdr_common.hdr_type, hdr->hdr_common.hdr_flags,
		 hdr->hdr_frag.hdr_frag_length, hdr->hdr_frag.hdr_frag_offset, hdr->hdr_frag.hdr_src_ptr.lval,
		 hdr->hdr_frag.hdr_src_ptr.pval, hdr->hdr_frag.hdr_dst_ptr.lval, hdr->hdr_frag.hdr_dst_ptr.pval );
    return;
 print_match_hdr:
    opal_output( 0, "%s hdr_common hdr_type %d hdr_flags %x\nmatch header hdr_contextid %d hdr_src %d hdr_dst %d hdr_tag %d\n             hdr_msg_length %lld hdr_msg_seq %d\n",
		 str, hdr->hdr_common.hdr_type, hdr->hdr_common.hdr_flags,
		 hdr->hdr_match.hdr_contextid, hdr->hdr_match.hdr_src, hdr->hdr_match.hdr_dst,
		 hdr->hdr_match.hdr_tag, hdr->hdr_match.hdr_msg_length, hdr->hdr_match.hdr_msg_seq );
    return;
 print_rndv_hdr:
    opal_output( 0, "%s hdr_common hdr_type %d hdr_flags %x\nrndv header hdr_contextid %d hdr_src %d hdr_dst %d hdr_tag %d\n            hdr_msg_length %lld hdr_msg_seq %d\n            hdr_frag_length %lld hdr_src_ptr (lval %lld, pval %p)\n",
		 str, hdr->hdr_common.hdr_type, hdr->hdr_common.hdr_flags,
		 hdr->hdr_rndv.hdr_match.hdr_contextid, hdr->hdr_rndv.hdr_match.hdr_src,
		 hdr->hdr_rndv.hdr_match.hdr_dst, hdr->hdr_rndv.hdr_match.hdr_tag,
		 hdr->hdr_rndv.hdr_match.hdr_msg_length, hdr->hdr_rndv.hdr_match.hdr_msg_seq,
		 hdr->hdr_rndv.hdr_frag_length, hdr->hdr_rndv.hdr_src_ptr.lval, hdr->hdr_rndv.hdr_src_ptr.pval);
    return;
}
