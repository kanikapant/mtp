/*
 *	TMEM BACKEND IMPLEMENTATION
 *	
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/kvm_types.h>
#include <linux/cleancache.h>
#include <linux/tmem.h>
#include <linux/spinlock.h>
#include <linux/slab.h>
 #include <linux/kobject.h>
#include <linux/string.h>
#include <linux/sysfs.h>
#include "tmem_bknd.h"
#include "custom_tree_op.c"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Kanika");

static struct kobject *kobj_main;
static struct kobject *kobj_client;
static struct kobject *kobj_pool;
static struct tmem_client *tmem_all_clients[MAX_CLIENTS];
struct kmem_cache* tmem_page_descriptors_cachep;
struct kmem_cache* tmem_objects_cachep;
struct kmem_cache* tmem_oid_cachep;

static unsigned long int tmem_bknd_indices_eviction(uint16_t client_id, int32_t pool_id, struct tmem_oid *oidp, long int no_of_indexes);
static unsigned long int tmem_bknd_objects_eviction(uint16_t client_id, int32_t pool_id, struct tmem_oid *oidp);
u64 tmem_puts;
u64 succ_tmem_puts;
u64 failed_tmem_puts;

u64 tmem_gets;
u64 succ_tmem_gets;
u64 failed_tmem_gets;

//static char write_buf[256];
static char finput[256];
//object accounting

static ssize_t  eviction_param_show(struct kobject *kobj, struct kobj_attribute *attr,char *buf)
{
		 return scnprintf(buf, 256, "%s\n", finput);	
}

static ssize_t eviction_param_store(struct kobject *kobj, struct kobj_attribute *attr,const char *buf, size_t count)
{


	int len = 0;
	uint16_t client_id; 
	int32_t pool_id ;
	struct tmem_oid *oidp;
	long int no_of_indexes;
	uint64_t oid[3];
	int flag;
	tmem_oid_cachep =kmem_cache_create("tmem_bknd_tmem_oid",sizeof(struct tmem_oid), 0, 0, NULL);
	oidp = kmem_cache_alloc(tmem_oid_cachep, GFP_ATOMIC);
	
	strcpy(finput, buf);
	

	sscanf(finput, "%u %u %llu %llu %llu %ld %d", &client_id , &pool_id , oid+2, oid+1 ,oid , &no_of_indexes , &flag);
	
	oidp->oid[2] = oid[2];
	oidp->oid[1] = oid[1];
	oidp->oid[0] = oid[0];
	
	if(flag==0)
		tmem_bknd_indices_eviction(client_id,  pool_id, oidp, no_of_indexes);
	else
		tmem_bknd_objects_eviction(client_id,  pool_id, oidp);
	return count;

}

/*****************************************************************************************************************************************************
															*********TMEM_BKND MAIN SYSFS ATTRIBUTE*********
******************************************************************************************************************************************************
*/


static ssize_t  accounting_info_client_show(struct kobject *kobj, struct kobj_attribute *attr,char *buf)
{
		 return 0;
}

static ssize_t accounting_info_client_store(struct kobject *kobj, struct kobj_attribute *attr,const char *buf, size_t count)
{
	return count;
}

/*****************************************************************************************************************************************************
															*********TMEM_BKND MAIN SYSFS ATTRIBUTE*********
******************************************************************************************************************************************************
*/


static ssize_t  accounting_info_pool_show(struct kobject *kobj, struct kobj_attribute *attr,char *buf)
{
		 return 0;
}

static ssize_t accounting_info_pool_store(struct kobject *kobj, struct kobj_attribute *attr,const char *buf, size_t count)
{
	return count;
}









/*****************************************************************************************************************************************************
															*********TMEM_BKND CLIENT SYSFS ATTRIBUTE*********
******************************************************************************************************************************************************
*/

static struct kobj_attribute tmem_bknd_client_attribute =
		__ATTR(accounting_info_client, 0664, accounting_info_client_show,accounting_info_client_store);

static struct attribute *attrs_client[] = {
	&tmem_bknd_client_attribute.attr,
	NULL,
};

static struct attribute_group attr_group_client = {
		.attrs = attrs_client,
};



/*****************************************************************************************************************************************************
															*********TMEM_BKND POOL SYSFS ATTRIBUTE*********
******************************************************************************************************************************************************
*/
//static struct kobj_attribute tmem_bknd_pool_attribute =
//		__ATTR(accounting_info_pool, 0664, accounting_info_pool_show,accounting_info_pool_store);
static struct kobj_attribute tmem_bknd_pool_attribute =
		__ATTR(eviction_param, 0664, eviction_param_show,eviction_param_store);




static struct attribute *attrs_pool[] = {
	&tmem_bknd_pool_attribute.attr,
	NULL,
};

static struct attribute_group attr_group_pool = {
		.attrs = attrs_pool,
};

/*
 TMEM HANDLING FUNCTIONS
 */


/*****************************************************************************************************************************************************
															*********NEW POOL HANDLER*********
******************************************************************************************************************************************************
*/

struct tmem_client* tmem_get_client_by_id(int client_id)
{
	//struct tmem_client* client = &tmem_bknd_host;
	struct tmem_client* client = NULL;

	//return NULL if Max number of clients exceeded
	if (client_id >= MAX_CLIENTS)
		goto out;

	//return a pointer to a client 
	client = tmem_all_clients[client_id];
out:
	return client;
}


static unsigned long int tmem_bknd_new_pool(int client_id, uint64_t uuid_lo,uint64_t uuid_hi, uint32_t flags)
{
	int poolid  = -1;
	int ret;
	struct tmem_client *client = NULL;
	struct tmem_pool *pool;
	//static int i=0;
	char str[15];
	//i++;
	
	
	client = tmem_get_client_by_id(client_id);

	if(client == NULL)
	{
		//if(debug(tmem_bknd_new_pool))
			pr_info(" *** mtp: %s, %s, %d | Invalid Client| "
				"tmem_bknd_new_pool *** \n",
				__FILE__, __func__, __LINE__);
		goto out;
	}

		/*Allocate memory for new pool*/
	pool = kmalloc(sizeof(struct tmem_pool), GFP_ATOMIC);

	/*Exit if no memory allocated for new pool descriptor*/
	if(pool == NULL)
	{
		//if(debug(tmem_bknd_new_pool))
			pr_info(" *** mtp: %s, %s, %d | Pool creation failed : "
				"out of memory | tmem_bknd_new_pool *** \n",
				__FILE__, __func__, __LINE__);

		goto out;
	}
	/*Find first free pool index for this client*/
	for(poolid = 0; poolid < MAX_POOLS_PER_CLIENT; poolid++)
	{
		if(client->this_client_all_pools[poolid] == NULL)
			break;
	}

	/*Check if no free pool index available for this client*/
	if(poolid >= MAX_POOLS_PER_CLIENT)
	{
		//if(debug(tmem_new_pool))
			pr_info(" *** mtp: %s, %s, %d | Pool creation failed: "
				"Max pools allowed for client: %d exceeded | "
				"tmem_bknd_new_pool *** \n",
				 __FILE__, __func__, __LINE__,
				 client->client_id);

		kfree(pool);
		poolid = -1;
		goto out;
	}

	pool->associated_client = client;
	pool->pool_id = poolid;
	pool->uuid[0] = uuid_lo;
	pool->uuid[1] = uuid_hi;
	sprintf(str, "%d", poolid);
	kobj_pool = kobject_create_and_add(str, kobj_client);
	if(!kobj_pool)
		return -ENOMEM;
	
	ret = sysfs_create_group(kobj_pool, &attr_group_pool);
	
	if (ret)
		kobject_put(kobj_pool);

	//pool->obj_count = 0;
	//pool->obj_count_max = 0;

	tmem_new_pool(pool, flags);

	client->this_client_all_pools[poolid] = pool;
	pr_info(" *** mtp | Created new %s tmem pool, id=%d, client=%d | "
			"tmem_bknd_new_pool *** \n",
			flags & TMEM_POOL_PERSIST ? "persistent":"ephemeral",
			poolid, client_id);

out:
	
return (unsigned long int)poolid;
}

/*****************************************************************************************************************************************************
															*********PUT PAGE HANDLER*********
******************************************************************************************************************************************************
*/
static unsigned long int tmem_bknd_put_page(uint16_t client_id, int32_t pool_id, struct tmem_oid *oidp, uint32_t index, struct page *client_page)
{
	struct	tmem_client *client;
	struct	tmem_pool *pool;
	struct	tmem_object_root *obj=NULL;
	struct	tmem_page_descriptor *pgi=NULL;
	unsigned int oidp_hash = tmem_oid_hash(oidp);
	int ret;
	tmem_puts++;
	//Getting the client object for the given client_id
	client = tmem_get_client_by_id(client_id);
	
	//Getting the pool object for the given pool_id
	pool = client->this_client_all_pools[pool_id];

	//Searching for the file object in the rb_tree for the file objects
	obj = tmem_obj_find(pool , oidp);

	/*This object search can result into 2 outcomes:
		1.Object present in the rb_tree
		2.Object not present in the rb_tree
	*/
	if(obj != NULL)
		{
			//Search for the index in the radix tree:
			pgi = tmem_pgp_lookup_in_obj(obj , index);

			/*2 possibilities:
				1.index already present in the slot array of radix tree node
				2.index not present
			*/
				if(pgi == NULL)
				{
					pr_info(" *** mtp | Object: %llu %llu %llu ""already exists at rb_tree slot: %u of ""pool: %u of client: %u | but index: %u"" is new | tmem_bknd_put_page *** \n",
					oidp->oid[2], oidp->oid[1],
					oidp->oid[0], oidp_hash, pool->pool_id,
					client->client_id, index);
				}
		}	
	else
		{
			//Printing for debugging
			pr_info(" *** mtp | Object: %llu %llu %llu does not ""exist at rb_tree slot: %u of pool: %u of ""client: %u | tmem_bknd_put_page *** \n",
				oidp->oid[2], oidp->oid[1], oidp->oid[0],
				oidp_hash, pool->pool_id, client->client_id);

			obj = tmem_obj_alloc(pool ,oidp);
			

			if(obj == NULL)
			{
				pr_info(" *** mtp:	| failed to ""allocate new object: %llu %llu %llu | ""tmem_bknd_put_page *** \n"
					, oidp->oid[2],oidp->oid[1], oidp->oid[0]);

					goto out;
			}
			//obj is NULL
			ret = tmem_obj_rb_insert(&pool->obj_rb_root[oidp_hash] , obj);
			
		
		}
			pgi = tmem_pgp_alloc(obj);

			if(pgi == NULL)
			{

				pr_info(" *** mtp:	| could not allocate ""tmem pgp for index: %u, object: %llu %llu %llu"" rooted rb_tree slot: %u of pool: %u of ""client: %u | tmem_bknd_put_page *** \n",
				index, oidp->oid[2], oidp->oid[1], oidp->oid[0],
				oidp_hash, pool->pool_id, client->client_id);
			}

			//pgp is NULL
			ret = tmem_pgp_add_to_obj(obj, index, pgi);
			if (ret == -ENOMEM || ret != 0)
			{
				
				pr_info(" *** mtp:| could not add tmem pgp ""for index: %u, object: %llu %llu %llu rooted ""at rb_tree slot: %u of pool: %u of client: %u ""into the object | tmem_bknd_put_page *** \n",
				 index,oidp->oid[2], oidp->oid[1], oidp->oid[0],
				oidp_hash, pool->pool_id, client->client_id);
			
			}
			

			pgi->index = index;
			pgi->tmem_page = alloc_page(GFP_ATOMIC);

			if (pgi->tmem_page == NULL)
			{
		
				//eviction is required here and basically it is not eviction(deletion)


				pr_info(" *** mtp:	| could not add page ""descriptor for index: %u, object: %llu %llu ""%llu rooted at rb_tree slot: %u of pool: %u ""of client: %u into the object | ""tmem_bknd_put_page *** \n",
				index, oidp->oid[2], oidp->oid[1], oidp->oid[0],
				oidp_hash, pool->pool_id, client->client_id);

				ret = -ENOMEM;
			}
			/*
			This a potential place for eviction
			*/

			ret = tmem_copy_from_client(pgi->tmem_page , client_page);

			if ( ret < 0 )
			{

				pr_info(" *** mtp:| could not copy contents"" of page with index: %u, object: %llu %llu ""%llu rooted at rb_tree slot: %u of pool: %u ""of client: %u | tmem_bknd_put_page *** \n",
				index, oidp->oid[2], oidp->oid[1], oidp->oid[0],
				oidp_hash, pool->pool_id, client->client_id);
			}
			succ_tmem_puts++;
			return 0;

out:
	failed_tmem_puts++;
	return (unsigned long int)ret;
}


/*****************************************************************************************************************************************************
															*********GET PAGE HANDLER*********
******************************************************************************************************************************************************
*/

static unsigned long int tmem_bknd_get_page(uint16_t client_id, int32_t pool_id, struct tmem_oid *oidp, uint32_t index, struct page *client_page)
{

		//search for file object in the rb_tree
		struct	tmem_client *client;
		struct	tmem_pool *pool;
		struct	tmem_object_root *obj=NULL;
		struct	tmem_page_descriptor *pgp=NULL;
		unsigned int oidp_hash = tmem_oid_hash(oidp);
		int rc = -1;
		//int ret;
		//Getting the client object for the given client_id
		client = tmem_get_client_by_id(client_id);
		
		 //Getting the pool object for the given pool_id
		pool = client->this_client_all_pools[pool_id];
		
		
		tmem_gets++;

		pr_info(" *** mtp | Searching for object: %llu %llu %llu at ""rb_tree slot: %u of pool: %u of client: %u | ""tmem_bknd_get_page *** \n",
			oidp->oid[2], oidp->oid[1], oidp->oid[0],oidp_hash, pool->pool_id, client->client_id);
		
		obj = tmem_obj_find(pool,oidp);
		
		if ( obj == NULL )
		{
			pr_info("|object: %llu %llu %llu does not exist | tmem_bknd_get_page*** \n",oidp->oid[2], oidp->oid[1], oidp->oid[0]);
			goto out;
		}
			
			pr_info(" *** mtp | object: %llu %llu %llu found at ""rb_tree slot: %u of pool: %u of client: %u | ""tmem_bknd_get_page *** \n",
				oidp->oid[2], oidp->oid[1], oidp->oid[0],
				oidp_hash, pool->pool_id, client->client_id);

		pgp = tmem_pgp_delete_from_obj(obj, index);

		if ( pgp == NULL )
		{
			pr_info(" *** mtp: | could not delete tmem_bknd pgp ""for index: %u, object: %llu %llu %llu, rooted ""at rb_tree slot: %u of pool: %u of client: %u ""| tmem_bknd_get_page *** \n",
					index , oidp->oid[2], oidp->oid[1], oidp->oid[0],oidp_hash, pool->pool_id, client->client_id);

			goto out;
		}
		
		rc = tmem_copy_to_client(client_page, pgp->tmem_page);
		if ( rc <= 0 )
		{
				rc = -1;
				goto bad_copy;
		}
		tmem_pgp_free(pgp);
		succ_tmem_gets++;
		pr_info(" *** mtp:|tmem_bknd index: %u |found with object: %llu %llu %llu|rooted at rb_tree slot: %u|of pool: %u of client: %u | tmem_bknd_get_page *** \n"
					,index,oidp->oid[2], oidp->oid[1], oidp->oid[0],
					oidp_hash, pool->pool_id, client->client_id);
		return 0;

	bad_copy:
	out:
		failed_tmem_gets++;
		return (unsigned long)rc;

}



/*****************************************************************************************************************************************************
															*********CLIENT CREATION HANDLER*********
******************************************************************************************************************************************************
*/

static int tmem_create_client(int client_id)
{
		struct tmem_client *client = NULL;
		int ret;
		char str[15];
		sprintf(str, "%d", client_id);
		kobj_client = kobject_create_and_add(str, kobj_main);
		if(!kobj_client)
		return -ENOMEM;
	
		ret = sysfs_create_group(kobj_client, &attr_group_client);
	
		if (ret)
		kobject_put(kobj_client);

		if (client_id >= MAX_CLIENTS)
				goto out;

		client = tmem_get_client_by_id(client_id);

		/*a client already present at that id*/
		pr_info("New client created");
		if(client != NULL)
				goto out;
		
		client = kmalloc(sizeof(struct tmem_client), GFP_ATOMIC);
		
		if(client == NULL)
				goto out;

		memset(client, 0, sizeof(struct tmem_client));

		client->client_id = client_id;
		//client->allocated = 1;

		tmem_all_clients[client_id] = client;
		return 0;
out:
		return -1;
}

/************************************************************************************************************************************************************
													************EVICTION FOR SOME NUMBER OF INDICES*************
*************************************************************************************************************************************************************
*/
static unsigned long int tmem_bknd_objects_eviction(uint16_t client_id, int32_t pool_id, struct tmem_oid *oidp )
{
		struct	tmem_client *client = NULL;
		struct	tmem_pool *pool = NULL;
		struct	tmem_object_root *obj=NULL;
		struct radix_tree_iter iter;
		void **slot;
		long int count=0;
		unsigned long int ret =-1;
		struct radix_tree_root *root; 
		uint32_t index;
		client = tmem_get_client_by_id(client_id);
		if(client==NULL)
		{
			pr_info(" *** mtp: | No such client possible"
				" : %d | tmem_bknd_eviction *** \n ", client_id);
			goto out;
		}
		pool = client->this_client_all_pools[pool_id];

		if(pool==NULL)
		{
			pr_info(" *** mtp: | Not a valid pool"
				" : %d | tmem_bknd_eviction *** \n ", pool_id);
			goto out;
		}
		obj = tmem_obj_find(pool,oidp);
		if(obj==NULL)
		{
			pr_info(" *** mtp | Object: %llu %llu %llu does not "
				"exist for pool: %u of ""client: %u | tmem_bknd_eviction *** \n",
				oidp->oid[2], oidp->oid[1], oidp->oid[0],pool->pool_id, client->client_id);
		}
		if ( obj->tree_root.rnode != NULL )
		{
			root = &obj->tree_root;
			if(root == NULL)
				printk(KERN_INFO "There is no radix tree| tmem_bknd_eviction");
			else
			{
				//radix_tree_for_each_slot - iterate over non-empty slots
				count++;
				radix_tree_for_each_slot(slot, root, &iter, 0)
				{
					struct tmem_page_descriptor *pgp = radix_tree_deref_slot(slot);
					index = iter.index;
					radix_tree_delete(root, pgp->index);
					pr_info(" *** mtp | success, item at index %u present:|tmem_bknd_eviction *** \n",index);
					tmem_pgp_destroy(pgp);
				}
			}
				
		}

	pr_info("*** mtp |Total indexes removed are:|%ld tmem_bknd_eviction ***" , count);	
	return 0;

out:
	return ret;
}
/************************************************************************************************************************************************************
													************EVICTION FOR FULL OBJECTS*************
*************************************************************************************************************************************************************
*/

static unsigned long int tmem_bknd_indices_eviction(uint16_t client_id, int32_t pool_id, struct tmem_oid *oidp ,long int no_of_indexes)
{
		struct	tmem_client *client = NULL;
		struct	tmem_pool *pool = NULL;
		struct	tmem_object_root *obj=NULL;
		struct radix_tree_iter iter;
		void **slot;
		long int count=0;
		unsigned long int ret =-1;
		struct radix_tree_root *root; 
		uint32_t index;
		client = tmem_get_client_by_id(client_id);
		if(client==NULL)
		{
			pr_info(" *** mtp: | No such client possible"
				" : %d | tmem_bknd_eviction *** \n ", client_id);
			goto out;
		}
		pool = client->this_client_all_pools[pool_id];

		if(pool==NULL)
		{
			pr_info(" *** mtp: | Not a valid pool"
				" : %d | tmem_bknd_eviction *** \n ", pool_id);
			goto out;
		}
		obj = tmem_obj_find(pool,oidp);
		/*pr_info(" *** mtp | Object: %llu %llu %llu does not "
				"exist for pool: %u of ""client: %u | tmem_bknd_eviction *** \n",
				oidp->oid[2], oidp->oid[1], oidp->oid[0] , pool_id , client_id);*/
		if(obj==NULL)
		{
			pr_info(" *** mtp | Object: %llu %llu %llu does not "
				"exist for pool: %u of ""client: %u | tmem_bknd_eviction *** \n",
				oidp->oid[2], oidp->oid[1], oidp->oid[0],pool->pool_id, client->client_id);
		}
		root = &obj->tree_root;

		//pgp = tmem_pgp_alloc(obj);
		pr_info("root of the radix tree is:%u | tmem_bknd_eviction ***" ,root);
		if(root == NULL)
			printk(KERN_INFO "There is no radix tree| tmem_bknd_eviction");
		//radix_tree_for_each_slot - iterate over non-empty slots
		
		radix_tree_for_each_slot(slot, root, &iter, 0)
		{
				count++;
				struct tmem_page_descriptor *pgp = radix_tree_deref_slot(slot);
				index = iter.index;
				if(count>no_of_indexes)
				{
					break;
				}
				else
				{
					pr_info("Index is:%u |tmem_bknd_eviction\n" ,index);
					radix_tree_delete(root, pgp->index);
					pr_info(" *** mtp | success, item at index %u present:|tmem_bknd_eviction *** \n",index);
					tmem_pgp_destroy(pgp);
				}
			
		}
		pr_info("*** mtp |Total indexes removed are:|%ld tmem_bknd_eviction ***" , count);	
	return 0;

out:
	return ret;
}



/*****************************************************************************************************************************************************
															*********TMEM DESTROY POOL OPERATION*********
******************************************************************************************************************************************************
*/



static unsigned long int tmem_bknd_destroy_pool(int client_id, uint32_t pool_id)
{
	struct tmem_client* client = NULL;
	struct tmem_pool* pool;
	int ret = 0;

	if (pool_id >= MAX_POOLS_PER_CLIENT)
		goto out;

	client = tmem_get_client_by_id(client_id);

	if(client == NULL)
	{
		pr_info(" *** mtp: No such client "
				"possible: %d | tmem_bknd_destroy_pool *** \n ",
				client->client_id);

		goto out;
	}

	if(client->allocated == 0)
	{
			pr_info(" *** mtp: First time client: %d "
				"doing something other than NEW_POOL| "
				"tmem_bknd_destroy_pool *** \n ",
				client->client_id);

		goto out;
	}

	pool = client->this_client_all_pools[pool_id];

	if(pool == NULL)
	{
			pr_info(" *** mtp: Client: %d doesn't have"
				" a valid POOL | tmem_bknd_destroy_pool *** \n ",
				client->client_id);

		goto out;
	}

	client->this_client_all_pools[pool_id] = NULL;

	tmem_flush_pool(pool, client_id);
	pr_info(" *** mtp | Successfully destroyed pool: %d of client: "
                        "%d | tmem_bknd_destory_pool *** \n", pool_id, client_id);
	ret = 1;

out:
	return (unsigned long)ret;
}

/*****************************************************************************************************************************************************
															*********TMEM DESTROY CLIENT OPERATION*********
******************************************************************************************************************************************************
*/

static int tmem_bknd_destroy_client(int client_id)
{
	int poolid = -1;
	struct tmem_pool *pool = NULL;
	struct tmem_client *client = NULL;
	int ret = -1;

	client = tmem_get_client_by_id(client_id);

	if(client == NULL)
	{
			pr_info(" *** mtp: No such client possible: "
				"%d | ktb_destroy_client *** \n ",
				 client_id);

		goto out;
	}

	if(client->allocated == 0)
	{
			pr_info(" *** mtp: First time client: %d "
				"doing something other than NEW_POOL| "
				"ktb_destroy_client *** \n ",
				 client_id);
		goto out;
	}

	for(poolid = 0, ret = 0; poolid < MAX_POOLS_PER_CLIENT; poolid++)
	{
		if(client->this_client_all_pools[poolid] != NULL)
		{
			pool = client->this_client_all_pools[poolid];
			tmem_flush_pool(pool, client_id);
			client->this_client_all_pools[poolid] = NULL;
			ret++;
		}
	}

	if(ret == 0)
	{
			pr_info(" *** mtp: client: %d does not have "
				"any valid pools | ktb_destroy_client *** \n ", client_id);
	}

	client->allocated = 0;
        kfree(client);
        tmem_all_clients[client_id] = NULL;
		pr_info(" *** mtp | successfully destroyed client: %d, flushed:"
			" %d pools | ktb_destroy_client *** \n ",
			client_id, ret);
out:
	return ret;
}








/*****************************************************************************************************************************************************
															*********TMEM OPERATIONS REGISTRATION*********
******************************************************************************************************************************************************
*/

static struct kvm_host_tmem_ops tmem_bknd_ops = {
	.kvm_host_new_pool = tmem_bknd_new_pool,
	.kvm_host_destroy_pool = tmem_bknd_destroy_pool,
	.kvm_host_put_page = tmem_bknd_put_page,
	.kvm_host_get_page = tmem_bknd_get_page,
	.kvm_host_create_client = tmem_create_client,
	.kvm_host_destroy_client = tmem_bknd_destroy_client,

};



/*****************************************************************************************************************************************************
															*********TMEM_BKND MAIN SYSFS ATTRIBUTE*********
******************************************************************************************************************************************************
*/

static struct kobj_attribute tmem_bknd_attribute =
		__ATTR(accounting_info_pool, 0664, accounting_info_pool_show,accounting_info_pool_store);

static struct attribute *attrs[] = {
	&tmem_bknd_attribute.attr,
	NULL,
};

static struct attribute_group attr_group= {
		.attrs = attrs,
};


/*****************************************************************************************************************************************************
															*********MODULE INIT*********
******************************************************************************************************************************************************
*/

static int __init tmem_bknd_init(void)
{
	int ret;
	kobj_main = kobject_create_and_add("tmem_bknd", NULL);
	//kobj1 = kobject_create_and_add("client1", tmem_bknd_kobj);
	printk(KERN_INFO"tmem_bknd: INSERTED *********************** INSERTED\n");
	printk(KERN_INFO"kvm_tmem_bknd_enabled: %d, use_cleancache: %d\n", kvm_tmem_bknd_enabled, use_cleancache);

	if(!kobj_main)
		return -ENOMEM;
	
	ret = sysfs_create_group(kobj_main, &attr_group);
	
	if (ret)
		kobject_put(kobj_main);


	if(kvm_tmem_bknd_enabled && use_cleancache) 
	{
		tmem_objects_cachep =
			kmem_cache_create("tmem_bknd_tmem_objects",sizeof(struct tmem_object_root), 0, 0, NULL);

		tmem_page_descriptors_cachep =
			kmem_cache_create("tmem_bknd_page_descriptors",sizeof(struct tmem_page_descriptor), 0, 0, NULL);

		kvm_host_tmem_register_ops(&tmem_bknd_ops);
	}				

	return 0;
}

/*****************************************************************************************************************************************************
															*********MODULE EXIT*********
******************************************************************************************************************************************************
*/
static void __exit tmem_bknd_exit(void)
{
	tmem_bknd_ops.kvm_host_new_pool = NULL;
	tmem_bknd_ops.kvm_host_put_page = NULL;
	tmem_bknd_ops.kvm_host_get_page = NULL;
	kvm_host_tmem_deregister_ops();
	//dereference the count of the object and is necessary for freeing the object
	kobject_put(kobj_pool);
	kobject_put(kobj_client);
	kobject_put(kobj_main);
	kobject_del(kobj_pool);
	kobject_del(kobj_client);
	kobject_del(kobj_main);
	pr_info("Number of Puts\t%llu\n" , tmem_puts);
	pr_info("Number of successfull Puts\t%llu\n" , succ_tmem_puts);
	pr_info("Number of unsuccessfull Puts\t%llu\n" , failed_tmem_puts);
	pr_info("Number of Gets\t%llu\n" , tmem_gets);
	pr_info("Number of successfull Gets\t%llu\n" , succ_tmem_gets);
	pr_info("Number of unsuccessfull Gets\t%llu\n" , failed_tmem_gets);
	printk(KERN_INFO"tmem_bknd: REMOVED **tmem_bknd********************* REMOVED\n");
}

module_init(tmem_bknd_init)
module_exit(tmem_bknd_exit)