/*
 * $HEADER$
 */
/*%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%*/

#include "lam/lfc/list.h"
#include "mca/mpi/ptl/base/ptl_base_fragment.h"

static void mca_ptl_base_frag_init(mca_ptl_base_frag_t* frag);
static void mca_ptl_base_frag_destroy(mca_ptl_base_frag_t* frag);


lam_class_info_t mca_ptl_base_frag_cls = { 
    "mca_ptl_base_frag_t", 
    &lam_list_item_cls,
    (class_init_t) mca_ptl_base_frag_init, 
    (class_destroy_t) mca_ptl_base_frag_destroy 
};

static void mca_ptl_base_frag_init(mca_ptl_base_frag_t* frag)
{
    SUPER_INIT(frag, &lam_list_item_cls);
}

static void mca_ptl_base_frag_destroy(mca_ptl_base_frag_t* frag)
{
    SUPER_DESTROY(frag, &lam_list_item_cls);
}

