/**********************************************************************
 * Copyright (C) 2000-2004 by Etnus, LLC.
 * Copyright (C) 1999 by Etnus, Inc.
 * Copyright (C) 1997-1998 Dolphin Interconnect Solutions Inc.
 *
 * Permission is hereby granted to use, reproduce, prepare derivative
 * works, and to redistribute to others.
 *
 *				  DISCLAIMER
 *
 * Neither Dolphin Interconnect Solutions, Etnus LLC, nor any of their
 * employees, makes any warranty express or implied, or assumes any
 * legal liability or responsibility for the accuracy, completeness,
 * or usefulness of any information, apparatus, product, or process
 * disclosed, or represents that its use would not infringe privately
 * owned rights.
 *
 * This code was written by
 * James Cownie: Dolphin Interconnect Solutions. <jcownie@dolphinics.com>
 *               Etnus LLC <jcownie@etnus.com>
 **********************************************************************/

/* Update log
 *
 * May 19 1998 JHC: Changed the names of the structs now that we don't
 *              include this directly in mpi_interface.h
 * Oct 27 1997 JHC: Structure definitions for structures used to hold MPICH
 *              info required by the DLL for dumping message queues.
 */

/***********************************************************************
 * Information associated with a specific executable image
 */
typedef struct 
{
    const struct mqs_image_callbacks * image_callbacks;	/* Functions needed here */
    /* basic structures */
    struct {
        int size;
        struct {
            int opal_list_next;
        } offset;
    } opal_list_item_t;
    struct {
        int size;
        struct {
            int opal_list_sentinel;
        } offset;
    } opal_list_t;
    struct {
        int empty;
    } ompi_free_list_item_t;
    struct {
        int size;
        struct {
            int fl_elem_size;
            int fl_header_space;
            int fl_alignment;
            int fl_allocations;
            int fl_max_to_alloc;
        } offset;
    } ompi_free_list_t;
    /* requests structures */
    struct {
        int empty;
    } ompi_request_t;
    struct {
        int empty;
    } mca_pml_base_request_t;
    /* communicator structures */
    struct {
        int size;
        struct {
            int lowest_free;
            int size;
            int addr;
        } offset;
    } ompi_pointer_array_t;
    struct {
        int size;
        struct {
            int grp_proc_count;
            int grp_my_rank;
            int grp_flags;
        } offset;
    } ompi_group_t;
    struct {
        int size;
        struct {
            int c_name;
            int c_contextid;
            int c_my_rank;
            int c_local_group;
        } offset;
    } ompi_communicator_t;
    /* Fields in MPID_QHDR */
    int unexpected_offs;

    /* Fields in MPID_QUEUE */
    int first_offs;

    /* Fields in MPID_QEL */
    int context_id_offs;	
    int tag_offs;
    int tagmask_offs;
    int lsrc_offs;
    int srcmask_offs;
    int next_offs;
    int ptr_offs;

    /* Fields in MPIR_SQEL */
    int db_shandle_offs;
    int db_comm_offs;
    int db_target_offs;
    int db_tag_offs;
    int db_data_offs;
    int db_byte_length_offs;
    int db_next_offs;

    /* Fields in MPIR_RHANDLE */
    int is_complete_offs;
    int buf_offs;
    int len_offs;
    int datatype_offs;
    int comm_offs;
    int start_offs;

    /* in the embedded MPI_Status object */
    int count_offs;
    int MPI_SOURCE_offs;
    int MPI_TAG_offs;

    /* Fields in MPIR_Comm_list */
    int sequence_number_offs;
    int comm_first_offs;

    /* Fields in MPIR_COMMUNICATOR */
    int np_offs;
    int lrank_to_grank_offs;
    int send_context_offs;
    int recv_context_offs;
    int comm_next_offs;
    int comm_name_offs;
} mpi_image_info; 

/***********************************************************************
 * Information associated with a specific process
 */

typedef struct group_t
{
  mqs_taddr_t table_base;			/* Where was it in the process  */
  int     ref_count;				/* How many references to us */
  int     entries;				/* How many entries */
  int     *local_to_global;			/* The translation table */
} group_t;

/* Information for a single process, a list of communicators, some
 * useful addresses, and the state of the iterators.
 */
typedef struct 
{
  const struct mqs_process_callbacks * process_callbacks; /* Functions needed here */

  struct communicator_t *communicator_list;	/* List of communicators in the process */
  mqs_target_type_sizes sizes;			/* Process architecture information */

  /* Addresses in the target process */
  mqs_taddr_t send_queue_base;			/* Where to find the send message queues */
  mqs_taddr_t recv_queue_base;			/* Where to find the recv message queues */
  mqs_taddr_t sendq_base;			/* Where to find the send queue */
  mqs_taddr_t commlist_base;			/* Where to find the list of communicators */

  /* Other info we need to remember about it */
  mqs_tword_t communicator_sequence;		
  int has_sendq;

  /* State for the iterators */
  struct communicator_t *current_communicator;	/* Easy, we're walking a simple list */
    
  mqs_taddr_t   next_msg;			/* And state for the message iterator */
  mqs_op_class  what;				/* What queue are we looking on */
} mpi_process_info;





