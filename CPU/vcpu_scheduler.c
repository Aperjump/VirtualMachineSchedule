#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <libvirt/libvirt.h>

#define THRESHOLD 10.0
#define MAXDIFF 1.0
typedef struct vcpu_stat {
    int domain;
    int number; // vcpu number
    int cpu; // real cpu number 
    unsigned long long cputime; // cpu time in nanosecond
    double tot_time;
} vcpu_stat;

typedef struct pcpu_stat {
    int vcpu_num; // number of virtual cpu for this pcpu
    unsigned long long time;
    double tot_time; //sum of all vcpu usage time
    double load; // 
    vcpu_stat* vcpu_list;
} pcpu_stat;
// compare vcpu
int vcpucompare(const void* , const void*);
// generate cpumap
void createmap(unsigned char*, int , int);

int main(int argc, char* argv[]) {
    if (argc != 2) {
        printf("please input schedule time interval\n");
        exit(1);
    }
    int interval = atoi(argv[1]);
    
    virConnectPtr conn = virConnectOpen("qemu:///system");
    if (!conn) {
        perror("Failed to get connected");
        exit(1);
    }
    int pcpu_num = virNodeGetCPUMap(conn, NULL, NULL, 0);
    int mapsize = VIR_CPU_MAPLEN(pcpu_num);
    unsigned char* cpumap = (unsigned char*)calloc(mapsize, sizeof(unsigned char));
    virDomainPtr* domain;
    unsigned int flags = VIR_CONNECT_LIST_DOMAINS_ACTIVE | VIR_CONNECT_LIST_DOMAINS_RUNNING;
    int domain_num = 0;
    int prev_domain_num = 0;
    pcpu_stat* prev_pcpu_list = NULL;
    pcpu_stat* pcpu_list = NULL;
    vcpu_stat* vcpu_list = NULL;
    vcpu_stat* prev_vcpu_list = NULL;
    while ((domain_num = virConnectListAllDomains(conn, &domain, flags)) > 0) {
        // exist virtual machine
        // i means physical vcpu
        int vcpu_num = 0;
        for (int i = 0; i < domain_num; i++)
            vcpu_num += virDomainGetMaxVcpus(domain[i]);
        pcpu_list = (pcpu_stat*)calloc(pcpu_num,  sizeof(pcpu_stat));
        vcpu_list = (vcpu_stat*)calloc(vcpu_num, sizeof(vcpu_stat));
        int counter = 0;
        for (int i = 0; i < domain_num; i++) {
            int cur_vcpu_num = virDomainGetMaxVcpus(domain[i]);
            virVcpuInfoPtr cur_vcpu_info_list =  (virVcpuInfo*)calloc(cur_vcpu_num, sizeof(virVcpuInfo));
            if (virDomainGetVcpus(domain[i], cur_vcpu_info_list, cur_vcpu_num, NULL, 0) < 0) {
                perror("cannot access vcpu");
                exit(1);
            }
            // j means current vcpu
            for (int j = 0; j < cur_vcpu_num; j++) {
                vcpu_list[counter + j].domain = i;
                vcpu_list[counter + j].number = cur_vcpu_info_list[j].number;
                vcpu_list[counter + j].cpu = cur_vcpu_info_list[j].cpu;
                vcpu_list[counter + j].cputime = cur_vcpu_info_list[j].cpuTime;
                if (prev_vcpu_list != NULL) {
                    printf("prev vcpu %d time is %lld, cur time is %lld\n", i, prev_vcpu_list[counter + j].cputime, vcpu_list[counter+j].cputime);
                }
                pcpu_list[vcpu_list[counter + j].cpu].vcpu_num += 1;
            }
            counter += cur_vcpu_num;
            //printf("cpu time for %d is %lld\n", cur_vcpu_info_list[0].cpu, 
            //        cur_vcpu_info_list[0].cpuTime);
            free(cur_vcpu_info_list);
        }
        for (int i = 0; i < pcpu_num; i++) {
            pcpu_list[i].vcpu_list = (vcpu_stat*)calloc(pcpu_list[i].vcpu_num, sizeof(vcpu_stat));
            int tmp = 0;
            for (int j = 0; j < domain_num; j++) {
                if (vcpu_list[j].cpu == i) {
                    pcpu_list[i].vcpu_list[tmp].domain = vcpu_list[j].domain;
                    pcpu_list[i].vcpu_list[tmp].number  = vcpu_list[j].number;
                    pcpu_list[i].vcpu_list[tmp].cpu = vcpu_list[j].cpu;
                    pcpu_list[i].vcpu_list[tmp].cputime = vcpu_list[j].cputime;
                    tmp++;
                }
            }
            printf("current pcpu: %d, number of vcpu: %d\n", i, pcpu_list[i].vcpu_num);
        }
        if (prev_domain_num != domain_num) {
            // balance begin
            int avgvcpu = (vcpu_num - 1 + pcpu_num) / pcpu_num;
            printf("before first run\n");
            for (int i = 0; i < vcpu_num; i++) {
                if (i % avgvcpu == 0)
                    createmap(cpumap, mapsize, i / avgvcpu);
                printf("current vcpu is %d, vcpu number is %d\n", i, vcpu_list[i].number);
                printf("cpumap is %u\n", cpumap[0]);
                virDomainPinVcpu(domain[i], vcpu_list[i].number, cpumap, mapsize);
            }
            /*
            for (int i = 0; i < pcpu_num; i++) {
                printf("current pcpu %d, number of vcpu is %d\n", i, pcpu_list[i].vcpu_num);
                for (int j = 0; j < pcpu_list[i].vcpu_num; j++) {
                    int domain_counter = pcpu_list[i].vcpu_list[j].domain;
                    printf("current domain is %d\n", domain_counter);
                    if (domain_counter % avgvcpu == 0)
                        createmap(cpumap, mapsize, domain_counter / avgvcpu);
                    virDomainPinVcpu(domain[domain_counter], vcpu_list[domain_counter].number, cpumap, mapsize);
                }
            }
            */
        } else {
            printf("start scheduling\n");
            for (int i = 0; i < vcpu_num; i++) {
                unsigned long long cur_time = vcpu_list[i].cputime;
                unsigned long long prev_time = prev_vcpu_list[i].cputime;
                double tot_time = 100.0 * (cur_time - prev_time) / (double)(interval * 1000000000);
                vcpu_list[i].tot_time = tot_time;
            }
            for (int i = 0; i < pcpu_num; i++) {
                double pcpu_sum = 0;
                for (int j = 0; j < pcpu_list[i].vcpu_num; j++) {
                    unsigned long long cur_time = pcpu_list[i].vcpu_list[j].cputime;
                    unsigned long long prev_time = prev_pcpu_list[i].vcpu_list[j].cputime;
                    double tot_time = 100.0 * (cur_time - prev_time) / (double)(interval * 1000000000);
                    pcpu_list[i].vcpu_list[j].tot_time = tot_time; 
                    printf("prev_time is %lld, cur_time is %lld\n", prev_time, cur_time);
                    pcpu_sum += tot_time;
                }
                pcpu_list[i].tot_time = pcpu_sum;
                printf("pcpu%d tot_time is %f\n", i, pcpu_sum);
            }
            double minusage = 100, maxusage = 0;
            int above_thres = 0;
            int minnum = -1, maxnum = -1;
            for (int i = 0; i < pcpu_num; i++) {
                above_thres |= (pcpu_list[i].tot_time > THRESHOLD);
                if (pcpu_list[i].tot_time > maxusage) {
                    maxusage = pcpu_list[i].tot_time;
                    maxnum = i;
                }
                if (pcpu_list[i].tot_time < minusage) {
                    minusage = pcpu_list[i].tot_time;
                    minnum = i;
                }
            }
            printf("maxusage: %f, minusage: %f\n", maxusage, minusage);
            if (above_thres && maxusage - minusage > MAXDIFF) {
                // each time put maxnum's first vcpu to minnum
                printf("begin schedule, current min pcpu is %d, max pcpu is %d", maxnum, minnum);
                vcpu_stat* copy_vcpu_list = (vcpu_stat*)calloc(vcpu_num, sizeof(vcpu_stat));
                memcpy(copy_vcpu_list, vcpu_list, vcpu_num);
                qsort(copy_vcpu_list, vcpu_num, sizeof(vcpu_stat), vcpucompare);
                for (int i = 0; i < vcpu_num; i++) {
                    int k = 0;
                    double minload = 100;
                    for (int j = 0; j < pcpu_num; j++) {
                        if (pcpu_list[j].load < minload) {
                            minload = pcpu_list[j].load;
                            k = j;
                        }
                    }
                    createmap(cpumap, mapsize, k);
                    virDomainPinVcpu(domain[copy_vcpu_list[i].domain], copy_vcpu_list[i].number, cpumap, mapsize);
                    pcpu_list[k].load += copy_vcpu_list[i].tot_time;
                }
                free(copy_vcpu_list);
            }
            
        }
        for (int i = 0; i < domain_num; i++) 
            virDomainFree(domain[i]);
        free(domain);
        if (prev_pcpu_list == NULL) {
            printf("first finish\n");
            prev_pcpu_list = pcpu_list;
            prev_domain_num = domain_num;
            prev_vcpu_list = vcpu_list;
            pcpu_list = NULL;
            vcpu_list = NULL;
            continue;
        }
        for (int i = 0; i < pcpu_num; i++) 
            free(prev_pcpu_list[i].vcpu_list);
        free(prev_pcpu_list);
        free(prev_vcpu_list);
        prev_pcpu_list = pcpu_list;
        prev_domain_num = domain_num;
        prev_vcpu_list = vcpu_list;
        vcpu_list = NULL;
        pcpu_list = NULL;
        sleep(interval);
    }

    for (int i = 0; i < domain_num; i++)
        free(pcpu_list[i].vcpu_list);
    free(pcpu_list);
    printf("domain number is %d\n", domain_num);
    virConnectClose(conn);

    printf("close yet\n");
    return 0;
}
int vcpucompare(const void* a, const void* b) {
    
    if ((*(vcpu_stat*)a).tot_time - (*(vcpu_stat*)b).tot_time < 0)
        return 1;
    if ((*(vcpu_stat*)a).tot_time - (*(vcpu_stat*)b).tot_time > 0)
        return -1;
    return 0;
}

void createmap(unsigned char* cpumap, int mapsize, int cpupos) {
    for (int i = 0; i < mapsize; i++) {
        if (i == cpupos / 8)
            cpumap[i] = 0x1 << (cpupos % 8);
        else
            cpumap[i] = 0x0;
    }
}
