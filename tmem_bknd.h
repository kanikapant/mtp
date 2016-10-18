#ifndef _TMEM_BKND_H_
#define _TMEM_BKND_H_

#include <linux/spinlock.h>
#include <linux/rwlock.h>
#include <linux/rbtree.h> 
#include <linux/radix-tree.h>
#include <linux/list.h>
#include <linux/tmem.h>

#define TMEM_CLIENT 1
#define MAX_CLIENTS 16
#define MAX_POOLS_PER_CLIENT 16
#define OBJ_HASH_BUCKETS 256
#define OBJ_HASH_BUCKETS_MASK (OBJ_HASH_BUCKETS-1)





/*
*******************************************************************************************************************************************************
														CLIENT OBJECT
*******************************************************************************************************************************************************
*/


struct tmem_client {
	/*Each pool corresponds to a fs in a client*/
	uint16_t client_id;

	struct tmem_pool *this_client_all_pools[MAX_POOLS_PER_CLIENT];
	bool allocated;

        
};

/*
*******************************************************************************************************************************************************
														POOL OBJECT
*******************************************************************************************************************************************************
*/
struct tmem_pool {

	struct tmem_client* associated_client;
	uint64_t uuid[2]; /* 0 for private, non-zero for shared */
	uint32_t pool_id;
	struct rb_root obj_rb_root[OBJ_HASH_BUCKETS]; 

	long obj_count;  /* atomicity depends on pool_rwlock held for write */
	long obj_count_max;
};


struct tmem_oid;
/*
*******************************************************************************************************************************************************
														TMEM FILE(INODE) OBJECT
*******************************************************************************************************************************************************

*/
struct tmem_object_root {
	struct tmem_oid oid;
	/*node containing the pointer to the address to the desired radix tree for that file object*/
	struct rb_node rb_tree_node; 	
	/*radix tree for pages within the file object(inode)*/
	struct radix_tree_root tree_root; 
	
	struct tmem_pool* pool;

	/*tmem_object_root accounting*/
	unsigned long objnode_count; 
	long pgp_count; 
};
/*
*******************************************************************************************************************************************************
														 TMEM_OBJECT_NODE
*******************************************************************************************************************************************************
*/

struct tmem_object_node {
	
	struct tmem_object_root *obj;
	//Containing node
	struct radix_tree_node rtn;
};

/*
*******************************************************************************************************************************************************
														INDEX OBJECT
*******************************************************************************************************************************************************
*/
struct tmem_page_descriptor{
	
	struct tmem_object_root *obj;
	
	uint32_t index;
	uint32_t size; 
	
	struct page* tmem_page;
};



/*
*******************************************************************************************************************************************************
														TMEM DATA STRUCTURES REQUIRED OPERATIONS
*******************************************************************************************************************************************************
*/
extern struct kmem_cache* tmem_page_descriptors_cachep;
extern struct kmem_cache* tmem_objects_cachep;

/*tmem pool functions*/
extern void tmem_new_pool(struct tmem_pool* , uint32_t );

extern void tmem_flush_pool(struct tmem_pool*, int);

/*tmem pool object functions*/
extern unsigned tmem_oid_hash(struct tmem_oid*);

extern struct tmem_object_root* tmem_obj_alloc(struct tmem_pool*,struct tmem_oid*);
extern struct tmem_object_root* tmem_obj_find(struct tmem_pool*,struct tmem_oid*);

extern int tmem_obj_rb_insert(struct rb_root*, struct tmem_object_root*);

extern void tmem_obj_free(struct tmem_object_root*);

extern void tmem_obj_destroy(struct tmem_object_root*);




/*tmem object page indices functions*/
extern struct tmem_page_descriptor* tmem_pgp_alloc(struct tmem_object_root*);

extern int tmem_pgp_add_to_obj(struct tmem_object_root*, uint32_t,struct tmem_page_descriptor*);


extern struct tmem_page_descriptor* tmem_pgp_lookup_in_obj(struct tmem_object_root* , uint32_t);

extern void tmem_pgp_free(struct tmem_page_descriptor*);
extern void tmem_pgp_free_data(struct tmem_page_descriptor *);    
extern struct tmem_page_descriptor* tmem_pgp_delete_from_obj(struct tmem_object_root*, uint32_t );  
extern void tmem_pgp_destroy(void *);
extern void custom_radix_tree_destroy(struct radix_tree_root * , void (*)(void *));

/*get client's page content functions*/                        
/*copy from client*/
extern int tmem_copy_from_client(struct page*, struct page*);
/*copy to client*/
extern int tmem_copy_to_client(struct page* client_page, struct page* page);     









/*
*******************************************************************************************************************************************************
																END
*******************************************************************************************************************************************************
*/


#endif