#include <linux/list.h>
#include <linux/slab.h>
#include <linux/tmem.h>
#include <linux/hash.h>
#include <linux/highmem.h>
#include <asm/spinlock.h>
#include <linux/radix-tree.h>
#include "tmem_bknd.h"




static void tmem_free_page(struct page* );

/******************************************************************************************************************************************************
															INSERTION INTO RB-TREE FOR TMEM OBJECTS
*******************************************************************************************************************************************************
*/
unsigned tmem_oid_hash(struct tmem_oid *oidp)
{
	return (hash_long(oidp->oid[0] ^ oidp->oid[1] ^ oidp->oid[2],
        	BITS_PER_LONG) & OBJ_HASH_BUCKETS_MASK);
}

static void oid_set_invalid(struct tmem_oid *oidp)
{
	oidp->oid[0] = oidp->oid[1] = oidp->oid[2] = -1UL;
}

static int tmem_oid_compare(struct tmem_oid *left, struct tmem_oid *right)
{
	if ( left->oid[2] == right->oid[2] )
	{
		if ( left->oid[1] == right->oid[1] )
		{
	    		if ( left->oid[0] == right->oid[0] )
				return 0;
	    		else if ( left->oid[0] < right->oid[0] )
				return -1;
	    		else
				return 1;
		}
		else if ( left->oid[1] < right->oid[1] )
	    		return -1;
		else
	    		return 1;
	}
	else if ( left->oid[2] < right->oid[2] )
		return -1;
	else
		return 1;
}

int tmem_obj_rb_insert(struct rb_root *root, struct tmem_object_root *obj)
{
	struct rb_node **new, *parent = NULL;
	struct tmem_object_root *this;

	new = &(root->rb_node);

	while ( *new )
	{
		this = container_of(*new, struct tmem_object_root, rb_tree_node);
		parent = *new;
		switch (tmem_oid_compare(&this->oid, &obj->oid))
		{
			case 0:
				return 0;
			case -1:
				new = &((*new)->rb_left);
				break;
			case 1:
				new = &((*new)->rb_right);
				break;
		}
	}

	rb_link_node(&obj->rb_tree_node, parent, new);
	rb_insert_color(&obj->rb_tree_node, root);
	return 1;
}




/******************************************************************************************************************************************************
															SEARCHING INTO RB-TREE FOR TMEM OBJECTS
*******************************************************************************************************************************************************
*/

struct tmem_object_root * tmem_obj_find(struct tmem_pool *pool,struct tmem_oid *oidp)
{
	struct rb_node *node;
	struct tmem_object_root *obj;

	//restart_find:
	//read_lock(&pool->pool_rwlock);
	//pr_info("*** hash(oidp): %u\n ***", tmem_oid_hash(oidp));
	node = pool->obj_rb_root[tmem_oid_hash(oidp)].rb_node;

	while ( node )
	{
		obj = container_of(node, struct tmem_object_root, rb_tree_node);
		switch ( tmem_oid_compare(&obj->oid, oidp) )
		{
		    case 0: /* equal */
			return obj;

		    case -1:
			node = node->rb_left;
			break;

		    case 1:
			node = node->rb_right;
		}
	}
	return NULL;
}



/******************************************************************************************************************************************************
															 RADIX TREE INSERTION
*******************************************************************************************************************************************************
*/

int tmem_pgp_add_to_obj(struct tmem_object_root *obj, uint32_t index,struct tmem_page_descriptor *pgp)
{
	int ret;

	ret = radix_tree_insert(&obj->tree_root, index, pgp);

	if ( !ret )
	{
		obj->pgp_count++;
	}

	return ret;
}


struct tmem_object_root* tmem_obj_alloc(struct tmem_pool* pool,struct tmem_oid *oidp)
{
	struct tmem_object_root *obj;
	obj = kmem_cache_alloc(tmem_objects_cachep, GFP_ATOMIC);

	if(obj == NULL)
		return NULL;

	pool->obj_count++;

	if (pool->obj_count > pool->obj_count_max)
		pool->obj_count_max = pool->obj_count;

	
	INIT_RADIX_TREE(&obj->tree_root, GFP_ATOMIC);
	
	obj->pool = pool;
	obj->oid = *oidp;
	obj->objnode_count = 0;
	obj->pgp_count = 0;
	return obj;
}




struct tmem_page_descriptor *tmem_pgp_alloc(struct tmem_object_root *obj)
{
	struct tmem_page_descriptor *pgp;
	struct tmem_pool *pool;

	
	pool = obj->pool;

	pgp = kmem_cache_alloc(tmem_page_descriptors_cachep, GFP_ATOMIC);

	if (pgp == NULL)
	{
		return NULL;
	}

	pgp->tmem_page = NULL;
	pgp->size = -1;
	pgp->index = -1;
	pgp->obj = obj;
	return pgp;
}


/**
*******************************************************************************************************************************************************
															 RADIX TREE SEARCH
*******************************************************************************************************************************************************
*/


struct tmem_page_descriptor *tmem_pgp_lookup_in_obj(struct tmem_object_root *obj, uint32_t index)
{
	
	return radix_tree_lookup(&obj->tree_root, index);
}

/*
*******************************************************************************************************************************************************
															 PAGE OPERATIONS
*******************************************************************************************************************************************************
*/

int tmem_copy_to_client(struct page* client_page, struct page* page)
{
	//map tmem_page and cli_page to va-s and do memcpy
	unsigned long *tmem_va, *client_va;
	int ret = 1;

	tmem_va = page_address(page);

	if(tmem_va == NULL)
		return -1;

	client_va = kmap_atomic(client_page);

	if(client_va == NULL)
		return -1;

	if(!memcpy(client_va, tmem_va, PAGE_SIZE))
		ret = -1;

	kunmap_atomic(client_va);
        smp_mb();
	return ret;
}

int tmem_copy_from_client(struct page* tmem_page, struct page* client_page)
{
	//map tmem_page and cli_page to va-s and do memcpy
	unsigned long *tmem_va, *client_va;
	int ret = 1;

	
	tmem_va = page_address(tmem_page);

	
	if(tmem_va == NULL)
		return -1;

	client_va = kmap_atomic(client_page);
	client_va = page_address(client_page);

	if(client_va == NULL)
		return -1;

        smp_mb();
	if(!memcpy(tmem_va, client_va, PAGE_SIZE))
		ret = -1;

	kunmap_atomic(client_va);

	return ret;
}

/*
*******************************************************************************************************************************************************
															 DELETE OPERATIONS
*******************************************************************************************************************************************************
*/

struct tmem_page_descriptor *tmem_pgp_delete_from_obj(struct tmem_object_root *obj, uint32_t index)
{
	struct tmem_page_descriptor *pgp;

	

    	pgp = radix_tree_delete(&obj->tree_root, index);

    	if ( pgp != NULL )
        	obj->pgp_count--;

    	return pgp;
}

void tmem_obj_free(struct tmem_object_root *obj)
{
	struct tmem_pool *pool;
	struct tmem_oid old_oid;
	
	pool = obj->pool;


	//may be a "stump" with no leaves
	if ( obj->tree_root.rnode != NULL )
		custom_radix_tree_destroy(&obj->tree_root, tmem_pgp_destroy);

	
	pool->obj_count--;
	

	obj->pool = NULL;
	old_oid = obj->oid;

	oid_set_invalid(&obj->oid);

	rb_erase(&obj->rb_tree_node, &pool->obj_rb_root[tmem_oid_hash(&old_oid)]);
	//spin_unlock(&obj->obj_spinlock);

	kmem_cache_free(tmem_objects_cachep, obj);
}

void tmem_pgp_free(struct tmem_page_descriptor *pgp)
{
	struct tmem_pool *pool = NULL;

	
	pool = pgp->obj->pool;

        /*
	if(can_show(tmem_pgp_free))
		pr_info(" *** mtp | freeing pgp of page with index: %u, "
			"of object: %llu %llu %llu in pool: %d, of client: %d "
			"| tmem_pgp_free *** \n",
			pgp->index, pgp->obj->oid.oid[2], pgp->obj->oid.oid[1],
			pgp->obj->oid.oid[0], pgp->obj->pool->pool_id,
			pgp->obj->pool->associated_client->client_id);
        */
	tmem_pgp_free_data(pgp);

	
	pgp->obj = NULL;
	pgp->index = -1;
	kmem_cache_free(tmem_page_descriptors_cachep, pgp);
    pr_info(" *** mtp | kmem_cache_free done | *** \n");
}
void tmem_pgp_free_data(struct tmem_page_descriptor *pgp)
{
	//uint32_t pgp_size = pgp->size;
	if(pgp->tmem_page == NULL)
		return;

	pr_info(" *** mtp | freeing data of pgp of page with index: %u, "
			"of object: %llu %llu %llu in pool: %d, of client: %d "
			"| tmem_pgp_free_data *** \n",
			pgp->index, pgp->obj->oid.oid[2], pgp->obj->oid.oid[1],
			pgp->obj->oid.oid[0], pgp->obj->pool->pool_id,
			pgp->obj->pool->associated_client->client_id);

	tmem_free_page(pgp->tmem_page);
	        //tmem_free_page(pgp->obj->pool, pgp->tmem_page);

	pgp->tmem_page = NULL;
	//pgp->size = -1;
}

static void tmem_free_page(struct page* page)
{
        if(page == NULL)
                BUG();
        else	
		__free_page(page);
}



void custom_radix_tree_destroy(struct radix_tree_root *root,void (*slot_free)(void *))
{
        struct radix_tree_iter iter;
        void **slot;

        radix_tree_for_each_slot(slot, root, &iter, 0)
        {
                //struct tmem_page_descriptor *pgp = 
                //        (struct tmem_page_descriptor *)*slot;
                struct tmem_page_descriptor *pgp = 
                        radix_tree_deref_slot(slot);
                //pr_info(" *** mtp | index being deleted: %u | "
                //        "custom_radix_tree_destroy *** \n",
                //         pgp->index);

                if(radix_tree_delete(root, pgp->index) == NULL)
                {
                      
                                pr_info(" *** mtp | item at index not present: "
                                        "%u | custom_radix_tree_destroy *** \n",
                                        pgp->index);
                } 
                else
                {
                      
                                pr_info(" *** mtp | success, item at index "
                                        "present: %u | "
                                        "custom_radix_tree_destroy *** \n",
                                        pgp->index);
                        slot_free(pgp);
                }

        }
}

void tmem_pgp_destroy(void *v)
{
	struct tmem_page_descriptor *pgp = (struct tmem_page_descriptor *)v;

	
	pgp->obj->pgp_count--;
	
		pr_info(" *** mtp | destroying pgp of page with index: %u, "
			"of object: %llu %llu %llu in pool: %d, of client: %d "
			"| tmem_pgp_destroy *** \n",
			pgp->index, pgp->obj->oid.oid[2], pgp->obj->oid.oid[1],
			pgp->obj->oid.oid[0], pgp->obj->pool->pool_id,
			pgp->obj->pool->associated_client->client_id);

        tmem_pgp_free(pgp);
}


static void tmem_pool_destroy_objs(struct tmem_pool *pool)
{
	struct rb_node *node;
	struct tmem_object_root *obj;
	int i;
	for (i = 0; i < OBJ_HASH_BUCKETS; i++)
	{
		node = rb_first(&pool->obj_rb_root[i]);
		while ( node != NULL )
		{
		    obj = container_of(node, struct tmem_object_root,rb_tree_node);
		    node = rb_next(node);
			pr_info(" *** mtp | destroying obj: %llu %llu %llu,"
				    " at slot: %d, of pool: %d, belonging to "
				    "client: %d | tmem_pool_destroy_objs *** \n",
				    obj->oid.oid[2], obj->oid.oid[1],
				    obj->oid.oid[0], i, pool->pool_id,
				    pool->associated_client->client_id);

		    tmem_obj_destroy(obj);
		}
	}
}


/*
*******************************************************************************************************************************************************
															 POOL OPERATIONS
*******************************************************************************************************************************************************
*/

void tmem_flush_pool(struct tmem_pool *pool, int client_id)
{
	pr_info(" *** mtp | Destroying ephemeral tmem pool: %d, of client: %d | "
		"tmem_flush_pool ***\n", pool->pool_id, client_id);

	tmem_pool_destroy_objs(pool);
	kfree(pool);
}

void tmem_new_pool(struct tmem_pool *pool, uint32_t flags)
{
	
	int i;
	for (i = 0; i < OBJ_HASH_BUCKETS; i++)
		pool->obj_rb_root[i] = RB_ROOT;

}



/*
*******************************************************************************************************************************************************
															 END
*******************************************************************************************************************************************************
*/













