#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <libvirt/libvirt.h>
static const int LEVEL_100 = 100 * 1024;
static const int LEVEL_200 = 200 * 1024;
static const int LEVEL_300 = 300 * 1024;
const double PROVIDER_RATIO = 0.75;
const double CONSUMER_RATIO = 0.5;
const double RELEASE_RATIO = 0.3;
const int FREEMEMORY_RATIO = 10;
typedef enum state {consumer, provider, stander} state;

int main(int argc, char** argv) {
    if (argc != 2) {
        perror("please input time interval");
        exit(1);
    }
    int interval = atoi(argv[1]);
    virConnectPtr conn = virConnectOpen("qemu:///system");
    if (!conn) {
        perror("Failed to get connected");
        exit(1);
    }
    unsigned int flags = VIR_CONNECT_LIST_DOMAINS_ACTIVE |
        VIR_CONNECT_LIST_DOMAINS_RUNNING;
    int domain_num = 0;
    virDomainPtr* domain;
    virNodeMemoryStats* paras;
    while((domain_num = virConnectListAllDomains(conn, &domain, flags)) > 0) {
        int paranum = 0; // number of parameters
        if (virNodeGetMemoryStats(conn, VIR_NODE_MEMORY_STATS_ALL_CELLS, NULL, &paranum, 0) == 0) {
            paras = (virNodeMemoryStats*)calloc(paranum, sizeof(virNodeMemoryStats));
            if (virNodeGetMemoryStats(conn, VIR_NODE_MEMORY_STATS_ALL_CELLS, paras, &paranum, 0)) {
                perror("cannot allocate memory stats");
                exit(1);
            }
        }
        unsigned long long *total_memory = (unsigned long long*)calloc(domain_num, sizeof(unsigned long long));
        unsigned long long *avail_memory = (unsigned long long*)calloc(domain_num, sizeof(unsigned long long));
        unsigned long long *unusd_memory = (unsigned long long*)calloc(domain_num, sizeof(unsigned long long));
        state* domain_type = (state*)calloc(domain_num, sizeof(state)); 
        for (int i = 0; i < domain_num; i++) {
            virDomainMemoryStatStruct memory_stat[VIR_DOMAIN_MEMORY_STAT_NR];
            if (virDomainSetMemoryStatsPeriod(domain[i], 1, VIR_DOMAIN_AFFECT_CURRENT) < 0) {
                perror("cannot change balloon driver");
                exit(1);
            }
            if (virDomainMemoryStats(domain[i], memory_stat, VIR_DOMAIN_MEMORY_STAT_NR, 0) < 0) {
                perror("no memory stat");
                exit(1);
            }
            for (int j = 0; j < VIR_DOMAIN_MEMORY_STAT_NR; j++) {
                if (memory_stat[j].tag == VIR_DOMAIN_MEMORY_STAT_ACTUAL_BALLOON)
                    total_memory[i] = memory_stat[j].val;
                else if (memory_stat[j].tag == VIR_DOMAIN_MEMORY_STAT_AVAILABLE)
                    avail_memory[i] = memory_stat[j].val;
                else if (memory_stat[j].tag == VIR_DOMAIN_MEMORY_STAT_UNUSED)
                    unusd_memory[i] = memory_stat[j].val;
            }
            /*
            total_memory[i] = memory_stat[VIR_DOMAIN_MEMORY_STAT_ACTUAL_BALLOON].val;
            avail_memory[i] = memory_stat[VIR_DOMAIN_MEMORY_STAT_AVAILABLE].val;
            unusd_memory[i] = memory_stat[VIR_DOMAIN_MEMORY_STAT_UNUSED].val;
            */
            if (unusd_memory[i] < CONSUMER_RATIO * total_memory[i])
                domain_type[i] = consumer;
            else if (unusd_memory[i] > PROVIDER_RATIO * total_memory[i])
                domain_type[i] = provider;
            else
                domain_type[i] = stander;
            printf("[Before]: domain %s total memory %lldMB, available memory %lldMB, unused memory %lldMB\n", virDomainGetName(domain[i]), total_memory[i]/1024, avail_memory[i]/1024, unusd_memory[i]/1024);
        }
        int consumer_counter = 0;
        int provider_counter = 0;
        unsigned int total_required = 0;
        unsigned int total_provide = 0;
        for (int i = 0; i < domain_num; i++) {
            if (domain_type[i] == consumer) {
                total_required += LEVEL_200;
                consumer_counter++;
                printf("domain %s is consumer\n", virDomainGetName(domain[i]));
            }
            if (domain_type[i] == provider) {
                unsigned long long alloc_memory = total_memory[i] - avail_memory[i] * RELEASE_RATIO;
                if (alloc_memory < LEVEL_200)
                    alloc_memory = LEVEL_200;
                virDomainSetMemory(domain[i], alloc_memory);
                total_provide += total_memory[i] - alloc_memory;
                printf("domain %s is provider\n", virDomainGetName(domain[i]));
            }
        }
        printf("required memory %uMB, provide memory %uMB\n", total_required/1024, total_provide/1024);
        long long system_memory = 0;
        if (total_required > total_provide) {
           unsigned long long prev_alloc = FREEMEMORY_RATIO * (total_required - total_provide);
           system_memory = virNodeGetFreeMemory(conn)/1024 - LEVEL_200;
           printf("remain system memory is %lld\n", system_memory/1024);
           if (system_memory > 0) {
               if (prev_alloc > system_memory)
                   prev_alloc = system_memory;
               total_provide += prev_alloc;
               printf("system_memory is %lld\n", prev_alloc/1024);
           }
        }
        //printf("add system_memory\n");
        if (total_required != 0) {
            unsigned long long per_memory = total_provide / consumer_counter;
            for (int i = 0; i < domain_num; i++) {
                if (domain_type[i] == consumer) {
                    unsigned long long maxmemory = virDomainGetMaxMemory(domain[i]);
                    unsigned long long provide_memory = total_memory[i] + per_memory;
                    if (provide_memory > maxmemory)
                        provide_memory = maxmemory;
                    virDomainSetMemory(domain[i], provide_memory);
                }
             }
        }
        /*
        for (int i = 0; i < domain_num; i++)
            if ((double)unusd_memory[i] / total_memory[i] * 100 > 35)
                virDomainSetMemory(domain[i], total_memory[i] - unusd_memory[i] / 2);
        for (int i = 0; i < domain_num; i++) {
            if ((double)unusd_memory[i] / total_memory[i] * 100 < 20) {
                if ((double)paras[1].value / paras[0].value * 100 > 50) {
                    if (total_memory[i] + LEVEL_200 > virDomainGetMaxMemory(domain[i]))
                        virDomainSetMemory(domain[i], virDomainGetMaxMemory(domain[i]));
                    else
                        virDomainSetMemory(domain[i], total_memory[i] + LEVEL_200);
                }
                else {
                    if (total_memory[i] + LEVEL_100 > virDomainGetMaxMemory(domain[i]))
                        virDomainSetMemory(domain[i], virDomainGetMaxMemory(domain[i]));
                    else
                        virDomainSetMemory(domain[i], total_memory[i] + LEVEL_100);
                }
                printf("domain %d increase memory\n", i);
            }
            printf("[After]: domain %d has unusd memory %lld, total memory %lld\n", i,unusd_memory[i] / 1024, total_memory[i] / 1024);
        }
        */
        free(paras);
        free(total_memory);
        free(avail_memory);
        free(unusd_memory);
        free(domain_type);
        for (int i = 0; i < domain_num; i++) 
            virDomainFree(domain[i]); 
        free(domain);
        printf("finish one pass\n");
        sleep(interval);
    }
    virConnectClose(conn);
    printf("close connection\n");
    return 0;
}
