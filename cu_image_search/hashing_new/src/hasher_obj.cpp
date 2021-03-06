#include "hasher_obj.hpp"

#include <stdio.h>
#include <opencv2/opencv.hpp>
#include <fstream>
#include <exception>

using namespace std;
using namespace cv;

int HasherObject::load_hashcodes() {
    double t_start = get_wall_time();
    cout << "[load_hashcodes] Loading hashcodes... " << endl;
    // Read DB hashcodes
    itq.release();
    itq.create(data_num, int_num, CV_32SC1);
    char * read_pos = (char*)itq.data;
    for (int i=0; i < update_hash_files.size(); i++)
    {
        read_in.open(update_hash_files[i].c_str(),ios::in|ios::binary);
        if (!read_in.is_open())
        {
            cout << "[load_hashcodes] Cannot load the itq updates! File "<< update_hash_files[i] << endl;
            return -1;
        }
        read_size = sizeof(int)*data_nums[i]*int_num;
        read_in.read(read_pos, read_size);
        read_in.close();
        read_pos +=read_size;
    }
    // Time loading
    double t_load_hashcodes = get_wall_time() - t_start;
    t[0] += t_load_hashcodes;
    cout << "[load_hashcodes] Loaded " << itq.rows << " DB Hashcodes in " << t_load_hashcodes << "s." << endl;
    return 0;
}


int HasherObject::load_itq_model() {
    double t_start = get_wall_time();
    cout << "[load_itq_model] Loading hash model... " << endl;
    // Read itq model (W, mvec)
    // read W
    read_in.open(pm.W_name.c_str(), ios::in|ios::binary);
    if (!read_in.is_open())
    {
        cout << "[load_itq_model] Cannot load the W model from " << pm.W_name << endl;
        return -1;
    }
    W.release();
    W.create(feature_dim, bit_num, CV_64F);
    read_size = sizeof(double)*feature_dim*bit_num;
    read_in.read((char*)W.data, read_size);
    read_in.close();
    // read mvec
    read_in.open(pm.mvec_name.c_str(), ios::in|ios::binary);
    if (!read_in.is_open())
    {
        cout << "[load_itq_model] Cannot load the mvec model from " << pm.mvec_name << endl;
        return -1;
    }
    mvec.release();
    mvec.create(1, bit_num, CV_64F);
    read_size = sizeof(double)*bit_num;
    read_in.read((char*)mvec.data, read_size);
    read_in.close();
    // Time loading
    double t_load_hashmodel = get_wall_time() - t_start;
    t[0] += t_load_hashmodel;
    cout << "[load_itq_model] Loaded hash model in " << t_load_hashmodel << "s." << endl;
    return 0;
}


Mat HasherObject::read_feats_from_disk(string filename) {
    // Calling this function means we start a new querying process, reset timings.
    reset_timings();
    double t_start = get_wall_time();
    // Read count
    int feats_num = (int)filesize(filename)/4/feature_dim;
    cout << "[read_feats_from_disk] Reading " << feats_num << " features from " << filename << endl;
    // Check input file
    ifstream read_in(filename.c_str(), ios::in|ios::binary);
    if (!read_in.is_open())
    {
        cout << "[read_feats_from_disk] Cannot load the feature file: " << filename << endl;
        return Mat();
    }
    // Allocate memory
    Mat feats_mat(feats_num, feature_dim, CV_32F);
    // Read features
    size_t read_size = sizeof(float)*feature_dim*feats_num;
    read_in.read((char*)feats_mat.data, read_size);
    // Finalize reading
    read_in.close();
    // Time reading feats
    double t_load_feats = get_wall_time() - t_start;
    t[1] = t_load_feats;
    cout << "[read_feats_from_disk] Read " << read_size <<  " bytes for " << feats_num << " features in " << t_load_feats << "s." << endl;
    return feats_mat;
}


void HasherObject::set_query_feats_from_disk(string filename) {
    query_feats.release();
    query_feats = read_feats_from_disk(filename);
}


// compute hashcodes from feats
unsigned int* HasherObject::compute_hashcodes_from_feats(Mat feats_mat) {
    double t_start = get_wall_time();
    if (norm) {
        for (int k = 0; k < query_num; k++) {
    	    //cout << "[compute_hashcodes_from_feats] Normalizing query: " << k+1 << "/" << query_num << endl;
            normalize((float*)feats_mat.data + k*feature_dim, feature_dim);
        }
    }
    // Allocate temporary matrices
    Mat feats_mat_double;
    feats_mat.convertTo(feats_mat_double, CV_64F);
    int feats_num = feats_mat_double.rows;
    int feats_dim = feats_mat_double.cols;
    cout << "[compute_hashcodes_from_feats] Computing hashcodes for " << feats_num << " features (dim: " << feats_dim << ")." << endl;
    // Project features
    Mat mvec_rep;
    mvec_rep = repeat(mvec, feats_num, 1);
    Mat realvalued_hash = feats_mat_double*W-mvec_rep;
    mvec_rep.release();
    // Binarizing features
    unsigned int * hash_mat = new unsigned int[int_num*feats_num];
    for  (int k=0; k < feats_num; k++)
    {
        for (int i=0; i < int_num; i++)
        {
            hash_mat[k*int_num+i] = 0;
            for (int j=0;j<32;j++)
                if (realvalued_hash.at<double>(k,i*32+j)>0)
                    hash_mat[k*int_num+i] += 1<<j;
        }
    }
    // Time hashcodes computation
    double t_compute_hashcodes = get_wall_time() - t_start;
    t[2] = t_compute_hashcodes;
    cout << "[compute_hashcodes_from_feats] Hashcodes computed in " << t_compute_hashcodes << "s." << endl;
    // Done hashing features
    return hash_mat;
}


std::vector< std::vector< std::pair<float,int> > > HasherObject::find_knn_nodiskout() {
    query_num = query_feats.rows;
    query_codes = compute_hashcodes_from_feats(query_feats);
    unsigned int* query = query_codes;
    float* query_feature = (float*)query_feats.data;
    vector<mypair> top_hamming;
    vector< vector< mypairf > > out_res;
    double t_start;
    int k;
    for (k=0; k < query_num; k++)
    {
        cout <<  "[find_knn_nodiskout] Looking for similar images of query #" << k+1 << endl;
        // Compute hamming distances between query k and all DB hashcodes
        cout <<  "[find_knn_nodiskout] Computing hamming distances for query #" << k+1 << endl;
        top_hamming = compute_hamming_dist_onehash(query);
        // Rerank based on real valued features
        cout <<  "[find_knn_nodiskout] Reranking for query #" << k+1 << endl;
        postrank = rerank_knn_onesample(query_feature, top_hamming);
        cout <<  "[find_knn_nodiskout] Pushing output for query #" << k+1 << endl;
        t_start = get_wall_time();
        out_res.push_back(postrank);
        t[7] += get_wall_time() - t_start;
        query += int_num;
        query_feature += feature_dim;
    }
    cout <<  "[find_knn_nodiskout] Done searching knn for " << k << " queries." << endl;
    // Clean up
    delete[] query_codes;
    query_feats.release();
    // Print out timing
    print_timing();
    return out_res;
}

// query methods
// use member query_feats and query_codes
void HasherObject::find_knn() {
    query_num = query_feats.rows;
    query_codes = compute_hashcodes_from_feats(query_feats);
    unsigned int* query = query_codes;
    float* query_feature = (float*)query_feats.data;
    vector<mypair> top_hamming;
    init_output_files();
    double t_start;
    int k;
    int print_div=100;
    cout <<  "[find_knn] Looking for similar images of " << query_num << " queries..." << endl;
    for (k=0; k < query_num; k++)
    {
        //cout <<  "[find_knn] Looking for similar images of query #" << k+1 << endl;
        // Compute hamming distances between query k and all DB hashcodes
        //cout <<  "[find_knn] Computing hamming distances for query #" << k+1 << " of " << query_num << endl;
        top_hamming = compute_hamming_dist_onehash(query);
        // Rerank based on real valued features
        //cout <<  "[find_knn] Reranking for query #" << k+1 << " of " << query_num << endl;
        postrank = rerank_knn_onesample(query_feature, top_hamming);
        // Write out results
        //cout <<  "[find_knn] Writing output for query #" << k+1 << " of " << query_num << endl;
        t_start = get_wall_time();
        // Cannot write out more than 2??
        write_to_output_file(postrank, hamming);
        t[7] += get_wall_time() - t_start;
        query += int_num;
        query_feature += feature_dim;
        if (((query_num/print_div)>0) && (k % (query_num/print_div) == 0) && (k > 0)) {
            cout <<  "[find_knn] Looking for similar images. Processed " << k << " images over " << query_num << " queries." << endl;
            print_timing();
        }
    }
    cout <<  "[find_knn] Done searching knn for " << query_num << " queries." << endl;
    // Clean up
    delete[] query_codes;
    query_feats.release();
    close_output_files();
    // Print out timing
    print_timing();
}

void HasherObject::print_timing() {
    cout << "[offline] Time reading DB hashcodes (seconds): " << t[0] << endl;
    cout << "[query] Time reading query feats (seconds): " << t[1] << endl;
    cout << "[query] Time computing query hashcodes (seconds): " << t[2] << endl;
    cout << "[query] Time hamming distances computation (accumulated for all queries) (seconds): " << t[3] << endl;
    cout << "[query] Time sorting hamming distances (accumulated for all queries) (seconds): " << t[4] << endl;
    cout << "[query] Time loading top features (accumulated for all queries) (seconds): " << t[5] << endl;
    cout << "[query] Time reranking top features (accumulated for all queries) (seconds): " << t[6] << endl;
    cout << "[query] Time saving results to disk (accumulated for all queries) (seconds): " << t[7] << endl;
}

void HasherObject::find_knn_from_feats(Mat _query_feats) {
    query_feats.release();
    query_feats = _query_feats;
    find_knn();
}

// compute rerank top samples using real valued features
vector<mypairf> HasherObject::rerank_knn_onesample(float* query_feature, vector<mypair> top_hamming) {
    double t_start = get_wall_time();
    //cout << "[rerank_knn_onesample] We have " << top_hamming.size() << " candidates to rerank." << endl;
    vector<mypairf> postrank(top_hamming.size());
    char* feature_p = (char*)top_feature_mat.data;
    read_size = sizeof(float)*feature_dim;
    int status = 0;
    int failed = 0;
    int i;
    // Getting top nn features
    for (i = 0; i < top_hamming.size(); i++)
    {
        status = get_onefeatcomp(top_hamming[i].second, read_size, accum, read_in_compfeatures, read_in_compidx, feature_p+i*read_size);
        if (status == -1) {
            cout << "[rerank_knn_onesample] Could not get feature " << top_hamming[i].second << ". Exiting." << endl;
            failed++;
        }
    }
    if (failed > 0) {
        return vector<mypairf>(0);
    }
    t[5] += get_wall_time() - t_start;

    // Reranking
    t_start = get_wall_time();
    //cout << "[rerank_knn_onesample] reraking using euclidean distance" << endl;
    #pragma omp parallel for num_threads(4)
    for (int i = 0; i < top_hamming.size(); i++)
    {
        postrank[i] = mypairf(0.0f,top_hamming[i].second);
        float* data_feature = (float*)top_feature_mat.data+feature_dim*i;
        for (int j=0;j<feature_dim;j++)
        {
            postrank[i].first += pow(query_feature[j]-data_feature[j],2);
        }
        postrank[i].first = postrank[i].first/2.0;
    }
    
    // Should we time separately this sort?
    std::sort(postrank.begin(), postrank.end(), comparatorf);
    t[6] += get_wall_time() - t_start;
    return postrank;
}

vector<mypair> HasherObject::compute_hamming_dist_onehash(unsigned int* query) {
    double t_start = get_wall_time();
    // Initialize data pointer
    unsigned int * hash_data = (unsigned int*)itq.data;
    unsigned int * tmp_hash_data = hash_data;

    // Compute distance for each sample of the DB
    #pragma omp parallel for num_threads(4)
    for (int i=0; i < data_num; i++)
    {
        // Pointer to ith DB hashcode
        tmp_hash_data = hash_data + i*int_num;
        // Initialize hamming distance 0 and sample id i
        hamming[i] = mypair(0,i);
        // Compute hamming distance by sets 32 bits
        for (int j=0; j<int_num; j++)
        {
            unsigned int xnor = query[j]^tmp_hash_data[j];
            hamming[i].first += NumberOfSetBits(xnor);
        }
        
    }
    t[3] += get_wall_time() - t_start;
    
    unsigned long long out_size = min((unsigned long long)top_feature, (unsigned long long)hamming.size());
    
    // Sort results
    t_start = get_wall_time();
    //sort(hamming.begin(), hamming.end(), comparator);
    // Use nth_element or partial_sort maybe?
    nth_element(hamming.begin(), hamming.begin()+out_size, hamming.end(), comparator);
    t[4] += get_wall_time() - t_start;

    // Only get the results we need
    vector<mypair>::const_iterator ho_first = hamming.begin();
    vector<mypair>::const_iterator ho_last = hamming.begin()+out_size;
    vector<mypair> hamming_out(ho_first, ho_last);

    // // For debugging
    // int small_hd_sort = hamming[0].first;
    // int big_hd_sort = hamming[out_size-1].first;
    // int small_hd = hamming_out[0].first;
    // int big_hd = hamming_out[out_size-1].first;
    // cout << "Top " << out_size << " sorted hamming distances range is [" << small_hd_sort << ", " << big_hd_sort << "]" << endl;
    // cout << "Top " << hamming_out.size() << " hamming distances range is [" << small_hd << ", " << big_hd << "]" << endl;
    return hamming_out;
}


void HasherObject::write_to_output_file(vector<mypairf> postrank, vector<mypair> hamming) {
    // Output to file (this is slow, can take several seconds for one query!)
    // First, write samples ids
    //for (int i=0; i < postrank.size(); i++) {
    // iterator version is not really faster...
    for (vector<mypairf>::iterator it = postrank.begin(), end = postrank.end(); it != end; it++) {
        //outputfile << postrank[i].second << ' ';
        if ((near_dup_th<0.0) || ((near_dup_th>0.0) && (it->first<near_dup_th))) {
            outputfile << it->second << ' ';
            // Also output to detailed hamming file (for debugging)
            if (DEMO == 0) {
                //outputfile_hamming << postrank[i].second << ' ';
                outputfile_hamming << it->second << ' ';
            }
        }
        else {
            break;
        }
    }
    // Then distances
    for (int i=0; i < postrank.size(); i++) {
        if ((near_dup_th<0.0) || ((near_dup_th>0.0) && (postrank[i].first<near_dup_th))) {
            outputfile << postrank[i].first << ' ';
            // Also output hamming distances (for debugging)
            if (DEMO == 0) {
                outputfile_hamming << hamming[i].first << ' ';
            }
        }
        else {
            break;
        }
    }
    // for (int i=0; i < postrank.size(); i++) {
    //     outputfile << postrank[i].first << ' ';
    //     // Also output hamming distances (for debugging)
    //     if (DEMO == 0) {
    //         outputfile_hamming << hamming[i].first << ' ';
    //     }
    // }
    // Write end of lines to both files
    outputfile << endl;
    if (DEMO==0) {
        outputfile_hamming << endl;
    }
}

void HasherObject::init_output_files() {
    string outname_sim = outname+"-sim.txt";
    cout <<  "[set_output_files] Will write results to " << outname_sim << endl;
    outputfile.open(outname_sim.c_str(), ios::out);
    if (DEMO==0) {
        string outname_hamming = outname+"-hamming.txt";
        cout <<  "Will write detailed hamming results to " << outname_hamming << endl;
        outputfile_hamming.open(outname_hamming.c_str(), ios::out);
    }
}

void HasherObject::close_output_files() {
    outputfile.close();
    if (DEMO==0) {
        outputfile_hamming.close();
    }
}

void HasherObject::set_paths() {
    pm.set_paths(norm, bit_num);
}

int HasherObject::read_update_files() {
    // Reinitialize vector files
    update_hash_files.clear();
    update_compfeature_files.clear();
    update_compidx_files.clear();
    read_in_compfeatures.clear();
    read_in_compidx.clear();
    // Read update files list
    ifstream fu(pm.update_files_list.c_str(),ios::in);
    if (!fu.is_open())
    {
        cout << "No update! Was looking for " << pm.update_files_list << endl;
        perror("");
        return -1;
    }
    else
    {
        cout << "Reading from update file " << pm.update_files_list << endl;
        // Read all update infos
        string line;
        while (getline(fu, line)) {
            update_hash_files.push_back(pm.update_hash_prefix+line+pm.update_hash_suffix);
            update_compfeature_files.push_back(pm.update_compfeature_prefix+line+pm.update_compfeature_suffix);
            update_compidx_files.push_back(pm.update_compidx_prefix+line+pm.update_compidx_suffix);
        }
    }
    // Read comp features
    int status = 0;
    status = fill_vector_files(read_in_compfeatures, update_compfeature_files);
    if (status==-1) {
        std::cout << "Could not load compressed features properly. Exiting." << std::endl;
        // Should we clean here
        return -1;
    }
    status = fill_vector_files(read_in_compidx, update_compidx_files);
    if (status==-1) {
        std::cout << "Could not load compressed indices properly. Exiting." << std::endl;
        // Should we clean here
        return -1;
    }

    return 0;
}

void HasherObject::fill_data_nums_accum() {
    // clear any previous data count computed
    data_nums.clear();
    data_num = fill_data_nums(update_hash_files, data_nums, bit_num);
    cout << "[fill_data_nums_accum] We have " << data_num << " images indexed." << endl;
    // This induces a segfault when called from swig?
    //delete[] accum;
    accum = new int[data_nums.size()];
    fill_accum(data_nums, accum);
    // this will overwrite top_feature
    set_top_feature();
    hamming.resize(data_num);
}

void HasherObject::clean_compfeat_files() {
    for (int i = 1; i<data_nums.size();i++)
    {
        if (read_in_compfeatures[i]->is_open())
            read_in_compfeatures[i]->close();
        if (read_in_compidx[i]->is_open())
            read_in_compidx[i]->close();
        delete read_in_compfeatures[i];
        delete read_in_compidx[i];
    }
}

// // io from memory - TO BE DONE
// Need to use boost::python converter for cv::Mat?
// maybe later...
// Mat get_feats_from_memory(void* data);

// Mat get_hashcodes_from_memory(void* data);
