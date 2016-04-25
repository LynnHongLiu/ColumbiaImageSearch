import MySQLdb
import hashlib
import os
import happybase

def get_SHA1_from_MySQL(image_id):
    res_sha1 = None
    if image_id:
        # minimize the number of global variables using only global_var
        localhost=global_var['local_db_host']
        localuser=global_var['local_db_user']
        localpwd=global_var['local_db_pwd']
        localdb=global_var['local_db_dbname']
        # query
        db=MySQLdb.connect(host=localhost,user=localuser,passwd=localpwd,db=localdb)
        c=db.cursor()
        sql='SELECT sha1 FROM uniqueIds WHERE htid=\"{}\"'.format(image_id) 
        #print sql
        c.execute(sql)
        res=c.fetchall()
        if res:
            res_sha1=res[0][0]
    return res_sha1

def get_batch_SHA1_from_mysql(image_ids):
    res_sha1 = [None]*len(image_ids)
    if image_ids:
        # minimize the number of global variables using only global_var
        localhost=global_var['local_db_host']
        localuser=global_var['local_db_user']
        localpwd=global_var['local_db_pwd']
        localdb=global_var['local_db_dbname']
        # query with these ids
        db=MySQLdb.connect(host=localhost,user=localuser,passwd=localpwd,db=localdb)
        c=db.cursor()
        sql='SELECT sha1,htid FROM uniqueIds WHERE htid IN (%s)'
        #print sql
        c.execute(sql,','.join(image_ids))
        res=c.fetchall()
        for row in res:
            res_sha1[image_ids.index(str(row[1]))]=row[0]
    return res_sha1

def get_SHA1_from_file(filepath,delete_after=False):
    sha1 = hashlib.sha1()
    f = open(filepath, 'rb')
    try:
        sha1.update(f.read())
    finally:
        f.close()
    if delete_after:
        os.unlink(filepath)
    return sha1.hexdigest()

def compute_SHA1_for_cdr_id_from_tab_samples(cdr_id,tab_samples_name,logf=None):
    sha1hash = None
    # get image url
    with pool.connection(timeout=hbase_conn_timeout) as connection:
        tab_samples = connection.table(tab_samples_name)
        one_row = tab_samples.row(cdr_id)
    #print one_row
    doc = one_row['images:images_doc']
    jd = json.loads(doc)
    one_url = jd['obj_stored_url']
    if not one_url: 
        # save the info that the URL is empty somewhere?
        pass
    else: # download
        localpath = dlImage(one_url,logf)
        # compute sha1
        if localpath:
            # True for delete after
            sha1hash = get_SHA1_from_file(localpath,True) 
        else:
            if logf:
                logf.write("Could not download image from URL {} of cdrid {}.\n".format(one_url,cdr_id))
            else:
                print "Could not download image from URL {} of cdrid {}.".format(one_url,cdr_id)
    return sha1hash

def get_SHA1_from_hbase_imagehash(image_id,tab_hash_name='image_hash'):
    hash_row = None
    sha1hash = None
    if image_id:
        with pool.connection(timeout=hbase_conn_timeout) as connection:
            tab_hash = connection.table(tab_hash_name)
            hash_row = tab_hash.row(str(image_id))
    if hash_row:
        sha1hash = hash_row['image:hash']
    return sha1hash

def get_SHA1_from_image_id_or_cdr_id(image_id,cdr_id,tab_hash_name='image_hash',logf=None):
    if image_id:
        sha1hash = get_SHA1_from_hbase_imagehash(image_id,tab_hash_name)
    if not sha1hash:
        # HBase Hash row is empty. Trying to get SHA1 from MySQL.
        sha1hash = get_batch_SHA1_from_mysql(image_id)
        # Or recompute from image if failed.
        if not sha1hash and cdr_id:
            #print "Could not get SHA1 from MYSQL. Recomputing..."
            sha1hash = compute_SHA1_for_cdr_id_from_tab_samples(cdr_id,tab_samples_name,logf)
    return sha1hash

def save_SHA1_to_hbase_cdrid_sha1(cdr_id,sha1hash,tab_cdrid_sha1_name='ht_images_cdrid_to_sha1'):
    # new table indexed by cdrid
    if cdr_id and sha1hash and sha1hash!='NULL':
        with pool.connection(timeout=hbase_conn_timeout) as connection:
            tab_cdr_hash = connection.table(tab_cdrid_sha1_name)
            tab_cdr_hash.put(str(cdr_id), {'hash:sha1': sha1hash})

def save_SHA1_to_hbase_imagehash(image_id,sha1hash,tab_hash_name='image_hash'):
    # old table indexed by htid 'tab_hash'
    if image_id and sha1hash and sha1hash!='NULL':
        with pool.connection(timeout=hbase_conn_timeout) as connection:
            tab_hash = connection.table(tab_hash_name)
            tab_hash.put(str(image_id), {'image:hash': sha1hash})
    
def save_SHA1_to_hbase(image_id,cdr_id,sha1hash,tab_hash_name='image_hash',tab_cdrid_sha1_name='ht_images_cdrid_to_sha1'):
    # save in the two tables
    save_SHA1_to_hbase_imagehash(image_id,sha1hash,tab_hash_name)
    save_SHA1_to_hbase_cdrid_sha1(cdr_id,sha1hash,tab_cdrid_sha1_name)

def save_missing_sha1(image_id,cdr_id,tab_missing_sha1_name='ht_images_missing_sha1'):
    with pool.connection(timeout=hbase_conn_timeout) as connection:
        tab_missing_sha1 = connection.table(tab_missing_sha1_name)
        # TODO maybe list of info:cdr_id if already exists?
        if not tab_missing_sha1.row(str(image_id)):
            tab_missing_sha1.put(str(image_id), {'info:cdr_id': str(cdr_id)})

def get_batch_SHA1_from_imageids(image_ids,tab_hash_name='image_hash',logf=None):
    #print image_id,cdr_id
    if not image_ids:
        #logf.write("[get_batch_SHA1_from_imageids] image_ids is empty!\n")
        return None
    str_image_ids=[str(iid) for iid in image_ids if str(iid)]
    if not str_image_ids:
        #logf.write("[get_batch_SHA1_from_imageids] str_image_ids is empty!\n")
        return None
    hash_rows = None
    #if logf:
    #    logf.write("Looking for images: {}\n".format(",".join(str_image_ids)))
    with pool.connection(timeout=hbase_conn_timeout) as connection:
        # if logf:
        #     logf.write("Connection opened on port: {}\n".format(connection.port))
        tab_hash = connection.table(tab_hash_name)
        hash_rows = tab_hash.rows(str_image_ids)
    sha1hash=[]
    misssing_sha1=[]
    stillmissing_sha1=[]
    # check if we have all sha1 requested
    if len(hash_rows)==len(str_image_ids):
        # hash_rows should have kept the order of image_ids
        for iid,sha1 in hash_rows:
            sha1hash.append(sha1['image:hash'])
    else:
        # fill whatever we got up to now
        sha1hash=[None]*len(str_image_ids) 
        for iid,sha1 in hash_rows:
            sha1hash[str_image_ids.index(iid)]=sha1['image:hash']
        missing_sha1=[str_image_ids[iid] for iid in range(len(str_image_ids)) if sha1hash[iid] is None]
        # try to get the missing sha1 form mysql...
        if missing_sha1:
            sha1hash_sql = get_batch_SHA1_from_mysql(missing_sha1)
            for missid,iid in enumerate(missing_sha1):
                if sha1hash_sql[missid] is not None:
                    sha1hash[str_image_ids.index(iid)]=sha1hash_sql[missid]
                else:
                    stillmissing_sha1.append(iid)
    if len(hash_rows)!=len(str_image_ids) and [sha1 is not None for sha1 in sha1hash].count(True)>len(hash_rows): 
        sha1_hbase=[]
        for iid,sha1 in hash_rows:
            sha1_hbase.append(iid)
        new_sha1=[(str_image_ids[lid],sha1) for lid,sha1 in enumerate(sha1hash) if sha1 is not None and str_image_ids[lid] not in sha1_hbase]
    return sha1hash,stillmissing_sha1,new_sha1


def save_missing_SHA1_to_hbase_missing_sha1(missing_sha1,tab_missing_sha1_name='ht_images_missing_sha1'):
    # save the missing sha1
    if missing_sha1: 
        with pool.connection(timeout=hbase_conn_timeout) as connection:
            tab_missing_sha1 = connection.table(tab_missing_sha1_name)
            b = tab_missing_sha1.batch()
            for image_id in missing_sha1:
                b.put(str(image_id), {'info:cdr_id': ''})
            b.send()

def save_batch_SHA1_to_hbase_image_hash(new_sha1,tab_hash_name='image_hash'):
    # save the new sha1 we got
    with pool.connection(timeout=hbase_conn_timeout) as connection:
        tab_hash = connection.table(tab_hash_name)
        b = tab_hash.batch()
        for image_id,sha1 in new_sha1:
            b.put(str(image_id), {'image:hash': sha1})
        b.send()