#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <curl/curl.h>
#include <libxml/HTMLparser.h>
#include <libxml/parser.h>
#include <libxml/xpath.h>
#include <libxml/uri.h>
#include <search.h>
#include <semaphore.h>
#include <pthread.h>
#include <sys/time.h>


#define SEED_URL "http://ece252-1.uwaterloo.ca/lab4/"
#define ECE252_HEADER "X-Ece252-Fragment: "
#define BUF_SIZE 1048576  /* 1024*1024 = 1M */
#define BUF_INC  524288   /* 1024*512  = 0.5M */

#define CT_PNG  "image/png"
#define CT_HTML "text/html"
#define CT_PNG_LEN  9
#define CT_HTML_LEN 9

#define HASH_SIZE 500
#define STACK_SIZE 500

#define max(a, b) \
   ({ __typeof__ (a) _a = (a); \
       __typeof__ (b) _b = (b); \
     _a > _b ? _a : _b; })

typedef struct recv_buf2 {
    char *buf;       /* memory to hold a copy of received data */
    size_t size;     /* size of valid data in buf in bytes*/
    size_t max_size; /* max capacity of buf in bytes*/
    int seq;         /* >=0 sequence number extracted from http header */
                     /* <0 indicates an invalid seq number */
} RECV_BUF;

typedef struct process_url_output{
    int type; //0 is html and 1 is png
    int num_hrefs;
    char** href_list; 
    char* png;
    char* eurl;
} process_url_output;


htmlDocPtr mem_getdoc(char *buf, int size, const char *url);
xmlXPathObjectPtr getnodeset (xmlDocPtr doc, xmlChar *xpath);
process_url_output find_http(char *fname, int size, int follow_relative_links, const char *base_url);
size_t header_cb_curl(char *p_recv, size_t size, size_t nmemb, void *userdata);
size_t write_cb_curl3(char *p_recv, size_t size, size_t nmemb, void *p_userdata);
int recv_buf_init(RECV_BUF *ptr, size_t max_size);
int recv_buf_cleanup(RECV_BUF *ptr);
void cleanup(CURL *curl, RECV_BUF *ptr);
int write_file(const char *path, const void *in, size_t len);
CURL *easy_handle_init(RECV_BUF *ptr, const char *url);
process_url_output process_data(CURL *curl_handle, RECV_BUF *p_recv_buf, char* url);


htmlDocPtr mem_getdoc(char *buf, int size, const char *url)
{
    int opts = HTML_PARSE_NOBLANKS | HTML_PARSE_NOERROR | \
               HTML_PARSE_NOWARNING | HTML_PARSE_NONET;
    htmlDocPtr doc = htmlReadMemory(buf, size, url, NULL, opts);
    
    if ( doc == NULL ) {
        //printf("%s\n",url);
        //fprintf(stderr, "Document not parsed successfully.\n");
        return NULL;
    }
    return doc;
}

xmlXPathObjectPtr getnodeset (xmlDocPtr doc, xmlChar *xpath)
{
	
    xmlXPathContextPtr context;
    xmlXPathObjectPtr result;

    context = xmlXPathNewContext(doc);
    if (context == NULL) {
        printf("Error in xmlXPathNewContext\n");
        return NULL;
    }
    result = xmlXPathEvalExpression(xpath, context);
    xmlXPathFreeContext(context);
    if (result == NULL) {
        printf("Error in xmlXPathEvalExpression\n");
        return NULL;
    }
    if(xmlXPathNodeSetIsEmpty(result->nodesetval)){
        xmlXPathFreeObject(result);
        //printf("No result\n");
        return NULL;
    }
    return result;
}


process_url_output find_http(char *buf, int size, int follow_relative_links, const char *base_url)
{

    int i;
    htmlDocPtr doc;
    xmlChar *xpath = (xmlChar*) "//a/@href";
    xmlNodeSetPtr nodeset;
    xmlXPathObjectPtr result;
    xmlChar *href;
		

    process_url_output res;
    int count = 0;
    if (buf == NULL) {
        xmlFreeDoc(doc);
        res.num_hrefs = 0;
        return res;
    }

    

    doc = mem_getdoc(buf, size, base_url);
    result = getnodeset (doc, xpath);

    // if (doc==NULL){
    //     if (result == NULL){
    //         printf("ksadfhas\n");
    //     }
    // }


    if (result) {
        nodeset = result->nodesetval;
        res.href_list = malloc(sizeof(char*)*(nodeset->nodeNr));

        for (i=0; i < nodeset->nodeNr; i++) {
            href = xmlNodeListGetString(doc, nodeset->nodeTab[i]->xmlChildrenNode, 1);
            if ( follow_relative_links ) {
                xmlChar *old = href;
                href = xmlBuildURI(href, (xmlChar *) base_url);
                xmlFree(old);
            }
            if ( href != NULL && !strncmp((const char *)href, "http", 4) ) {
                count++;
                //res.href_list[i]=(char*)href;
                res.href_list[i]=malloc(sizeof(char)*257);
                strcpy(res.href_list[i], (char*)href);
                //printf("href: %s\n", href);
            }
            xmlFree(href);
        }
        xmlXPathFreeObject (result);
    }
    xmlFreeDoc(doc);
    //xmlCleanupParser();

    res.num_hrefs = count;
    res.type = 0;

    return res;
}
/**
 * @brief  cURL header call back function to extract image sequence number from 
 *         http header data. An example header for image part n (assume n = 2) is:
 *         X-Ece252-Fragment: 2
 * @param  char *p_recv: header data delivered by cURL
 * @param  size_t size size of each memb
 * @param  size_t nmemb number of memb
 * @param  void *userdata user defined data structurea
 * @return size of header data received.
 * @details this routine will be invoked multiple times by the libcurl until the full
 * header data are received.  we are only interested in the ECE252_HEADER line 
 * received so that we can extract the image sequence number from it. This
 * explains the if block in the code.
 */
size_t header_cb_curl(char *p_recv, size_t size, size_t nmemb, void *userdata)
{
    int realsize = size * nmemb;
    RECV_BUF *p = userdata;

#ifdef DEBUG1_
    printf("%s", p_recv);
#endif /* DEBUG1_ */
    if (realsize > strlen(ECE252_HEADER) &&
	strncmp(p_recv, ECE252_HEADER, strlen(ECE252_HEADER)) == 0) {

        /* extract img sequence number */
	p->seq = atoi(p_recv + strlen(ECE252_HEADER));

    }
    return realsize;
}


/**
 * @brief write callback function to save a copy of received data in RAM.
 *        The received libcurl data are pointed by p_recv, 
 *        which is provided by libcurl and is not user allocated memory.
 *        The user allocated memory is at p_userdata. One needs to
 *        cast it to the proper struct to make good use of it.
 *        This function maybe invoked more than once by one invokation of
 *        curl_easy_perform().
 */

size_t write_cb_curl3(char *p_recv, size_t size, size_t nmemb, void *p_userdata)
{
    size_t realsize = size * nmemb;
    RECV_BUF *p = (RECV_BUF *)p_userdata;
 
    if (p->size + realsize + 1 > p->max_size) {/* hope this rarely happens */ 
        /* received data is not 0 terminated, add one byte for terminating 0 */
        size_t new_size = p->max_size + max(BUF_INC, realsize + 1);   
        char *q = realloc(p->buf, new_size);
        if (q == NULL) {
            perror("realloc"); /* out of memory */
            return -1;
        }
        p->buf = q;
        p->max_size = new_size;
    }

    memcpy(p->buf + p->size, p_recv, realsize); /*copy data from libcurl*/
    p->size += realsize;
    p->buf[p->size] = 0;

    return realsize;
}


int recv_buf_init(RECV_BUF *ptr, size_t max_size)
{
    void *p = NULL;
    
    if (ptr == NULL) {
        return 1;
    }

    p = malloc(max_size);
    if (p == NULL) {
	return 2;
    }
    
    ptr->buf = p;
    ptr->size = 0;
    ptr->max_size = max_size;
    ptr->seq = -1;              /* valid seq should be positive */
    return 0;
}

int recv_buf_cleanup(RECV_BUF *ptr)
{
    if (ptr == NULL) {
	return 1;
    }
    
    free(ptr->buf);
    ptr->size = 0;
    ptr->max_size = 0;
    return 0;
}

void cleanup(CURL *curl, RECV_BUF *ptr)
{
        curl_easy_cleanup(curl);
        curl_global_cleanup();
        recv_buf_cleanup(ptr);
}
/**
 * @brief output data in memory to a file
 * @param path const char *, output file path
 * @param in  void *, input data to be written to the file
 * @param len size_t, length of the input data in bytes
 */

int write_file(const char *path, const void *in, size_t len)
{
    FILE *fp = NULL;

    if (path == NULL) {
        fprintf(stderr, "write_file: file name is null!\n");
        return -1;
    }

    if (in == NULL) {
        fprintf(stderr, "write_file: input data is null!\n");
        return -1;
    }

    fp = fopen(path, "wb");
    if (fp == NULL) {
        perror("fopen");
        return -2;
    }

    if (fwrite(in, 1, len, fp) != len) {
        fprintf(stderr, "write_file: imcomplete write!\n");
        return -3; 
    }
    return fclose(fp);
}

/**
 * @brief create a curl easy handle and set the options.
 * @param RECV_BUF *ptr points to user data needed by the curl write call back function
 * @param const char *url is the target url to fetch resoruce
 * @return a valid CURL * handle upon sucess; NULL otherwise
 * Note: the caller is responsbile for cleaning the returned curl handle
 */

CURL *easy_handle_init(RECV_BUF *ptr, const char *url)
{
    CURL *curl_handle = NULL;

    if ( ptr == NULL || url == NULL) {
        return NULL;
    }

    /* init user defined call back function buffer */
    if ( recv_buf_init(ptr, BUF_SIZE) != 0 ) {
        return NULL;
    }
    /* init a curl session */
    curl_handle = curl_easy_init();

    if (curl_handle == NULL) {
        fprintf(stderr, "curl_easy_init: returned NULL\n");
        return NULL;
    }

    /* specify URL to get */
    curl_easy_setopt(curl_handle, CURLOPT_URL, url);

    /* register write call back function to process received data */
    curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, write_cb_curl3); 
    /* user defined data structure passed to the call back function */
    curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, (void *)ptr);

    /* register header call back function to process received header data */
    curl_easy_setopt(curl_handle, CURLOPT_HEADERFUNCTION, header_cb_curl); 
    /* user defined data structure passed to the call back function */
    curl_easy_setopt(curl_handle, CURLOPT_HEADERDATA, (void *)ptr);

    /* some servers requires a user-agent field */
    curl_easy_setopt(curl_handle, CURLOPT_USERAGENT, "ece252 lab4 crawler");

    /* follow HTTP 3XX redirects */
    curl_easy_setopt(curl_handle, CURLOPT_FOLLOWLOCATION, 1L);
    /* continue to send authentication credentials when following locations */
    curl_easy_setopt(curl_handle, CURLOPT_UNRESTRICTED_AUTH, 1L);
    /* max numbre of redirects to follow sets to 5 */
    curl_easy_setopt(curl_handle, CURLOPT_MAXREDIRS, 5L);
    /* supports all built-in encodings */ 
    curl_easy_setopt(curl_handle, CURLOPT_ACCEPT_ENCODING, "");

    /* Max time in seconds that the connection phase to the server to take */
    //curl_easy_setopt(curl_handle, CURLOPT_CONNECTTIMEOUT, 5L);
    /* Max time in seconds that libcurl transfer operation is allowed to take */
    //curl_easy_setopt(curl_handle, CURLOPT_TIMEOUT, 10L);
    /* Time out for Expect: 100-continue response in milliseconds */
    //curl_easy_setopt(curl_handle, CURLOPT_EXPECT_100_TIMEOUT_MS, 0L);

    /* Enable the cookie engine without reading any initial cookies */
    curl_easy_setopt(curl_handle, CURLOPT_COOKIEFILE, "");
    /* allow whatever auth the proxy speaks */
    curl_easy_setopt(curl_handle, CURLOPT_PROXYAUTH, CURLAUTH_ANY);
    /* allow whatever auth the server speaks */
    curl_easy_setopt(curl_handle, CURLOPT_HTTPAUTH, CURLAUTH_ANY);

    return curl_handle;
}

process_url_output process_html(CURL *curl_handle, RECV_BUF *p_recv_buf, char* url)
{
    char fname[257];
    int follow_relative_link = 1;
    char *eurl = NULL; 
    pid_t pid =getpid();

    curl_easy_getinfo(curl_handle, CURLINFO_EFFECTIVE_URL, &eurl);

    process_url_output output;
    output.num_hrefs = 0;

    if (eurl != NULL){
        output = find_http(p_recv_buf->buf, p_recv_buf->size, follow_relative_link, eurl); 
        if(output.num_hrefs != 0){
            output.eurl = malloc(sizeof(char)*257);
            strcpy(output.eurl, eurl);
        }

    }
    
    
    return output;
}

process_url_output process_png(CURL *curl_handle, RECV_BUF *p_recv_buf, char* url)
{
    char *eurl = NULL;
    curl_easy_getinfo(curl_handle, CURLINFO_EFFECTIVE_URL, &eurl);

    process_url_output output;
    output.num_hrefs = 0;

    if ( eurl != NULL) {
        output.type = 1;
        output.num_hrefs = 1;
        output.href_list = malloc(sizeof(char*));
        output.href_list[0] = malloc(sizeof(char)*257);
        output.png = malloc(p_recv_buf->size);
        memcpy(output.png, p_recv_buf->buf, p_recv_buf->size);
        strcpy(output.href_list[0],eurl);

        output.eurl = malloc(sizeof(char)*257);
        strcpy(output.eurl, eurl);
    }

    return output;
}


process_url_output process_data(CURL *curl_handle, RECV_BUF *p_recv_buf, char* url)
{
    CURLcode res;
    char fname[257];
    pid_t pid =getpid();
    long response_code;

    process_url_output output;
    output.num_hrefs = 0;

    res = curl_easy_getinfo(curl_handle, CURLINFO_RESPONSE_CODE, &response_code);
    if ( res == CURLE_OK ) {
	    //printf("Response code: %ld\n", response_code);
    }

    if ( response_code >= 400 ) { 
    	//fprintf(stderr, "Error.\n");
        return output;
    }

    char *ct = NULL;
    res = curl_easy_getinfo(curl_handle, CURLINFO_CONTENT_TYPE, &ct);
    if ( res == CURLE_OK && ct != NULL ) {
    	//printf("Content-Type: %s, len=%ld\n", ct, strlen(ct));
    } else {
        fprintf(stderr, "Failed obtain Content-Type\n");
        return output;
    }
    if ( strstr(ct, CT_HTML) ) {
        return process_html(curl_handle, p_recv_buf, url);
    } else if ( strstr(ct, CT_PNG) ) {
        return process_png(curl_handle, p_recv_buf, url);
    } 

    

    return output;
}

 process_url_output process_url(char* url){
    CURL *curl_handle;
    CURLcode res;
    RECV_BUF recv_buf;

    process_url_output output;
    output.num_hrefs = 0;

    //curl_global_init(CURL_GLOBAL_DEFAULT);
    curl_handle = easy_handle_init(&recv_buf, url);

    if ( curl_handle == NULL ) {
        //fprintf(stderr, "Curl initialization failed. Exiting...\n");
        //curl_global_cleanup();
        abort();
    }
    /* get it! */
    res = curl_easy_perform(curl_handle);

    if( res != CURLE_OK) {
        //fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
        cleanup(curl_handle, &recv_buf);
        return output;
    } else {
	//printf("%lu bytes received in memory %p, seq=%d.\n", \
               recv_buf.size, recv_buf.buf, recv_buf.seq);
    }

    /* process the download data */
    output = process_data(curl_handle, &recv_buf, url);

    /* cleaning up */
    cleanup(curl_handle, &recv_buf);
    return output; 
}

typedef unsigned char U8;

int is_png(U8 *buf, size_t n){
    
    //make sure that buffer is 8 bytes
    if (n != 8){
        return 0;
    }

    U8 png_header[8] = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};

    //ensure that buffer is png_signature
    for (int i = 0; i < 8; i++){
        if (buf[i]!=png_header[i]){
            return 0;
        }
    }

    return 1;
}


typedef struct thread_args{
    int num_images;
    char** png_list;
    ENTRY log, *log_p;
    char **url_frontier_stack;
    int *url_frontier_sp;
    int *png_count;

    sem_t *stack_access;
    sem_t *png_list_access;
    sem_t *log_access;

    int *num_active_threads;

    char** hash_table_pointers;
    int* num_elements_in_hash_table;

    pthread_rwlock_t *num_active_threads_rw;


    
} thread_args;

typedef struct findpng2_return{
    char** png_list;
    char** hash_table_pointers;
    int* png_count;
    int* num_elements_in_hash_table;
}findpng2_return;

void* find_png_thread_function(void* args){
    thread_args* arguments = (thread_args*)args;

    int num_images = arguments->num_images;
    char** png_list = arguments->png_list;
    ENTRY log = arguments->log;
    ENTRY *log_p = arguments->log_p;
    char **url_frontier_stack = arguments->url_frontier_stack;
    int *url_frontier_sp = arguments->url_frontier_sp;
    int *png_count = arguments->png_count;
    sem_t *stack_access = arguments->stack_access;
    int *num_active_threads = arguments->num_active_threads;
    sem_t *png_list_access = arguments->png_list_access;
    char** hash_table_pointers = arguments->hash_table_pointers;
    int* num_elements_in_hash_table = arguments->num_elements_in_hash_table;
    sem_t *log_access = arguments->log_access;
    pthread_rwlock_t *num_active_threads_rw = arguments->num_active_threads_rw;


    while (1){
        //exit condition is if the stack pointer is at -1 or the png count is equal to the number of pngs specified

        sem_wait(png_list_access);
        if (*png_count == num_images){
            sem_post(png_list_access);
            break;
        }
        sem_post(png_list_access);

        char *cur_url = malloc(sizeof(char)*257);

        sem_wait(stack_access);

        if (*url_frontier_sp >= 0){
            pthread_rwlock_wrlock(num_active_threads_rw);  
            //printf("%d\n",*num_active_threads);
            (*num_active_threads)++;
            pthread_rwlock_unlock(num_active_threads_rw);  

            int temp_url_frontier_sp = *url_frontier_sp;
            strcpy(cur_url,url_frontier_stack[temp_url_frontier_sp]);
            (*url_frontier_sp)--;

        }else{
            sem_post(stack_access);
            pthread_rwlock_rdlock(num_active_threads_rw); 
            int temp_num_active_threads;
            temp_num_active_threads = (*num_active_threads);
            pthread_rwlock_unlock(num_active_threads_rw);

            if (temp_num_active_threads == 0){
                
                free(cur_url);
                break;
            }else{
                free(cur_url);
                usleep(1000);
                continue;
            }

            
        }

        sem_post(stack_access);

        

        


        //do da work
        process_url_output output = process_url(cur_url);


        if (output.num_hrefs==0){
            pthread_rwlock_wrlock(num_active_threads_rw); 
            (*num_active_threads)--;
            pthread_rwlock_unlock(num_active_threads_rw); 
            free(cur_url);
            continue;
        }

        if (strcmp(cur_url, output.eurl) != 0){
            sem_wait(log_access);
            log.key = output.eurl;
            log_p = hsearch(log, FIND);
            if (log_p != NULL){
                sem_post(log_access);
                if (output.type==1){ 
                    free(output.png);
                    free(output.href_list);
                }else if (output.type==0){
                    for (int i = 0; i < output.num_hrefs; i++){
                        free(output.href_list[i]);
                    }
                    free(output.href_list);
                }
                free(output.eurl);
                free(cur_url);

                pthread_rwlock_wrlock(num_active_threads_rw); 
                (*num_active_threads)--;
                pthread_rwlock_unlock(num_active_threads_rw); 

                continue;

                
            }else{
                strcpy(hash_table_pointers[*num_elements_in_hash_table], output.eurl);
                (*num_elements_in_hash_table)++;
                log.key = hash_table_pointers[(*num_elements_in_hash_table)-1];
                log.data = NULL;
                log_p = hsearch(log, ENTER);
            }
            sem_post(log_access);

            



        }

        if (output.type==1){

            if (output.num_hrefs > 0){
                char* header = malloc(sizeof(U8)*8);
                memcpy(header, output.png, sizeof(U8)*8);

                if (is_png(header, 8)){
                    sem_wait(png_list_access);
                    int png_insert = 1;
                    if (*png_count < num_images){
                        if (*png_count>=1){
                            for (int i = 0; i < *png_count; i++){
                                if (strcmp(png_list[i], output.href_list[0])==0){
                                    png_insert = 0;
                                    break;
                                }
                            }
                        }
                        if (png_insert==1){
                            strcpy(png_list[*png_count], output.href_list[0]);
                            (*png_count)++;
                        }
                        
                        
                    }


                    sem_post(png_list_access);
                    
                }

                free(header);
                free(output.png);
                free(output.href_list[0]);
                free(output.href_list);
                free(output.eurl);
            }   

        }else if (output.type==0){
            if (output.num_hrefs > 0){
                for (int i = 0; i < output.num_hrefs; i++){
                    int insert = 0;

                    sem_wait(log_access);
                    log.key = output.href_list[i];
                    log_p = hsearch(log, FIND);
                    if (log_p == NULL){
                        insert = 1;
                        strcpy(hash_table_pointers[*num_elements_in_hash_table], output.href_list[i]);
                        (*num_elements_in_hash_table)++;
                        log.key = hash_table_pointers[(*num_elements_in_hash_table)-1];
                        log.data = NULL;
                        log_p = hsearch(log, ENTER);
                    }    
                    sem_post(log_access);                
                    


                    if (insert == 1){
                        sem_wait(stack_access);
                        (*url_frontier_sp)++;
                        strcpy(url_frontier_stack[*url_frontier_sp], output.href_list[i]);
                        sem_post(stack_access);

                    }
                    

                    free(output.href_list[i]);
                }
                free(output.href_list);
                free(output.eurl);
            }

        }




        free(cur_url);

        pthread_rwlock_wrlock(num_active_threads_rw); 
        (*num_active_threads)--;
        pthread_rwlock_unlock(num_active_threads_rw); 



    }

}

findpng2_return findpng2(int num_threads, int num_images, char* log_file_name, char* seed){
    //create the three lists
    char **png_list = malloc(sizeof(char*)*num_images);
    char **url_frontier_stack = malloc(sizeof(char*)*STACK_SIZE);
    char **hash_table_pointers = malloc(sizeof(char*)*HASH_SIZE);
    int *num_elements_in_hash_table = malloc(sizeof(int));
    *num_elements_in_hash_table = 1;

    //make it so that the stack list and the hash table list are malloced with 257 chars for all 500 elements
    //then whenever needed just do a strcpy from the output list or whatever into the two lists
    //this would mean that each list has it's OWN copy of each url
    //then all the urls from the output list can be freed. will be a lot cleaner.
    for (int i = 0; i  < STACK_SIZE; i++){
        url_frontier_stack[i] = malloc(sizeof(char)*257);
        hash_table_pointers[i] = malloc(sizeof(char)*257);
    }

    for (int i = 0; i < num_images; i++){
        png_list[i] = malloc(sizeof(char)*257);
    }



    hcreate(HASH_SIZE);

    int *url_frontier_sp = malloc(sizeof(int));
    *url_frontier_sp = 0;
    strcpy(url_frontier_stack[0], seed);

    int *png_count = malloc(sizeof(int));
    *png_count = 0;

    ENTRY log, *log_p;

    //insert seed into the hash table
    strcpy(hash_table_pointers[0], seed);
    log.key = hash_table_pointers[0];
    log.data = NULL;
    log_p = hsearch(log, ENTER);


    pthread_t *p_tids = malloc(sizeof(pthread_t) * num_threads);
    thread_args *args = malloc(sizeof(thread_args) * num_threads);
    
    sem_t *stack_access = malloc(sizeof(sem_t));
    sem_t *png_list_access = malloc(sizeof(sem_t));
    sem_t *log_access = malloc(sizeof(sem_t));

    pthread_rwlock_t *num_active_threads_rw = malloc(sizeof(pthread_rwlock_t));
    pthread_rwlock_init(num_active_threads_rw, NULL);

    sem_init(stack_access, 0, 1);
    sem_init(png_list_access, 0, 1);
    sem_init(log_access, 0, 1);
 
    int *num_active_threads = malloc(sizeof(int));
    *num_active_threads = 0;

    for (int i = 0; i < num_threads; i ++){
        args[i].num_images = num_images;
        args[i].png_list = png_list;
        args[i].log = log;
        args[i].log_p = log_p;
        args[i].url_frontier_stack = url_frontier_stack;
        args[i].url_frontier_sp = url_frontier_sp;
        args[i].png_count = png_count;
        args[i].stack_access = stack_access;
        args[i].num_active_threads = num_active_threads;
        args[i].png_list_access = png_list_access;
        args[i].hash_table_pointers = hash_table_pointers;
        args[i].num_elements_in_hash_table = num_elements_in_hash_table;
        args[i].log_access = log_access;
        args[i].num_active_threads_rw = num_active_threads_rw;

        pthread_create(p_tids+i, NULL, find_png_thread_function, args+i);
    }

    for (int i = 0; i < num_threads; i++) {
        pthread_join(p_tids[i], NULL);
    }


    //deallocate everything
    for (int i = 0; i < STACK_SIZE; i++){
        if (url_frontier_stack[i]!=NULL){
            free(url_frontier_stack[i]);
        }
        
    }
    free(url_frontier_stack);
    hdestroy();  
    free(p_tids);
    free(args);

    sem_destroy(stack_access);
    free(stack_access);
    sem_destroy(png_list_access);
    free(png_list_access);
    sem_destroy(log_access);
    free(log_access);
    free(url_frontier_sp);


    free(num_active_threads);

    pthread_rwlock_destroy(num_active_threads_rw);
    free(num_active_threads_rw);



    findpng2_return result;

    result.png_count = png_count;
    result.num_elements_in_hash_table = num_elements_in_hash_table;
    result.hash_table_pointers=hash_table_pointers;
    result.png_list = png_list;

    return result;

    

}

int main( int argc, char** argv ) 
{

    double times[2];
    struct timeval tv;

    if (gettimeofday(&tv, NULL) != 0) {
        perror("gettimeofday");
        abort();
    }
    times[0] = (tv.tv_sec) + tv.tv_usec/1000000.;

    curl_global_init(CURL_GLOBAL_DEFAULT);

    //get command line arguments
    int c;
    int t = 1;
    int m = 50;
    char *v = NULL;
    char *str = "option requires an argument";
    
    while ((c = getopt (argc, argv, "t:m:v:")) != -1) {
        switch (c) {
        case 't':
	    t = strtoul(optarg, NULL, 10);
	    if (t <= 0) {
                fprintf(stderr, "%s: %s > 0 -- 't'\n", argv[0], str);
                return -1;
            }
            break;
        case 'm':
            m = strtoul(optarg, NULL, 10);
            if (m < 0) {
                fprintf(stderr, "%s: %s >= 0 -- 'm'\n", argv[0], str);
                return -1;
            }
            break;

        case 'v':
            v = optarg;
            break;
        default:
            return -1;
        }
    }

    char *url_to_use = argv[argc-1];
    //printf("%s\n", url_to_use);


    findpng2_return res = findpng2(t, m, v, url_to_use);

    FILE *png_file;

    png_file = fopen("png_urls.txt", "w");


    for (int i = 0; i < *(res.png_count); i ++){
        fprintf(png_file, "%s\n", res.png_list[i]);
    }

    fclose(png_file);

    if (v != NULL){
        FILE *log_file;

        log_file = fopen(v, "w");


        for (int i = 0; i < *(res.num_elements_in_hash_table); i ++){
            fprintf(log_file, "%s\n",res.hash_table_pointers[i]);
        }

        fclose(log_file);

        
    }

    for (int i = 0; i < m; i++){
        free(res.png_list[i]);
    }
    free(res.png_list); 
    free(res.png_count);

    for (int i = 0; i < STACK_SIZE; i++){
        free(res.hash_table_pointers[i]);
    }
    free(res.hash_table_pointers);
    free(res.num_elements_in_hash_table);
    
    xmlCleanupParser();
    curl_global_cleanup();


    if (gettimeofday(&tv, NULL) != 0) {
        perror("gettimeofday");
        abort();
    }

    times[1] = (tv.tv_sec) + tv.tv_usec/1000000.;
    printf("findpng2 execution time: %.6lf seconds\n", times[1] - times[0]);



    return 0;
}
