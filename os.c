#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <sys/resource.h>
#include "result.h"

#define TOTAL_FILES 600      	// 전체 가상 파일 개수
#define MAX_TASKS 700       	// 큐에 넣을 수 있는 최대 작업 수
#define TIME_MULTIPLIER 30000  // 알고리즘 시간 조절용 배수
#define MAX_FILES_PER_PROC 600

// 파일의 처리 단계 정의
typedef enum { RAW, BWT_DONE, MTF_DONE } Stage;

// 작업 구조체: 파일 이름, 데이터, 현재 단계 포함
typedef struct {
	char* name;
	char  data[256];
	Stage stage;
	int size;  // 파일 크기
} Task;

typedef struct {
	int indices[MAX_FILES_PER_PROC];
	int count;
	int total_size;
} ProcessLoad;

// 내부에서 사용될 작업 구조체
typedef struct {
	int index;
	int size;
} FileEntry;

// 단계별 큐
Task* raw_queue[MAX_TASKS], * bwt_queue[MAX_TASKS], * mtf_queue[MAX_TASKS];
int raw_head = 0, raw_tail = 0;
int bwt_head = 0, bwt_tail = 0;
int mtf_head = 0, mtf_tail = 0;

// 동기화 변수
pthread_mutex_t queue_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t queue_not_empty = PTHREAD_COND_INITIALIZER;

int completed_tasks = 0;
int task_target = 0;

// cpu 돌리는 시간
pthread_mutex_t complete_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t all_done = PTHREAD_COND_INITIALIZER;

// 파일 크기 배열 (1~100 범위의 임의 수)
/*
int file_sizes[TOTAL_FILES] = {
    56, 97, 40, 89, 53, 21, 30, 91, 31, 86,
    80, 64, 34, 98, 99, 11, 76, 95, 47, 58,
    36, 25, 62, 17, 43, 93, 54, 10, 32, 83,
    48, 39, 23, 46, 63, 55, 33, 35, 41, 73,
    15, 52, 79, 27, 29, 66, 13, 70, 20, 59,
    28, 74, 12, 68, 18, 44, 42, 78, 87, 50,
    16, 84, 71, 72, 45, 82, 37, 38, 49, 92,
    24, 85, 94, 26, 96, 19, 22, 61, 14, 57,
    100, 65, 60, 75, 90, 67, 51, 69, 77, 88,
    55, 74, 28, 59, 32, 83, 36, 11, 40, 30,
    10, 48, 72, 19, 89, 97, 98, 35, 12, 26,
    15, 93, 23, 46, 33, 16, 87, 84, 24, 38,
    76, 34, 79, 60, 50, 73, 20, 99, 18, 25,
    91, 80, 45, 65, 42, 41, 43, 17, 85, 64,
    14, 94, 37, 13, 27, 62, 96, 39, 66, 92,
    63, 21, 86, 44, 67, 22, 78, 53, 29, 31,
    90, 100, 95, 81, 69, 58, 70, 82, 47, 61,
    77, 71, 49, 51, 88, 52, 75, 54, 68, 56,
    84, 48, 55, 60, 33, 20, 79, 28, 43, 12,
    83, 96, 31, 36, 59, 39, 94, 16, 76, 42,
    38, 97, 65, 23, 18, 86, 30, 91, 24, 98,
    37, 80, 46, 58, 64, 81, 10, 26, 95, 41,
    66, 61, 40, 62, 13, 35, 69, 99, 25, 17,
    92, 87, 27, 90, 44, 85, 22, 93, 32, 74,
    70, 15, 50, 21, 78, 34, 19, 71, 19, 14,
    57, 77, 67, 68, 73, 11, 82, 98, 89, 29,
    45, 100, 63, 49, 66, 52, 35, 13, 47, 75,
    86, 46, 38, 36, 54, 31, 88, 85, 90, 51,
    70, 53, 17, 97, 72, 69, 43, 91, 30, 92,
    71, 83, 62, 84, 48, 27, 60, 87, 56, 76,
};*/

int file_sizes[TOTAL_FILES] = {
    // 소형 파일 (1-25) - 480개
    3, 7, 12, 5, 18, 2, 9, 15, 6, 21,
    4, 11, 8, 16, 3, 19, 7, 13, 10, 22,
    1, 14, 5, 20, 9, 17, 6, 23, 11, 4,
    8, 15, 2, 18, 12, 7, 24, 3, 16, 9,
    13, 6, 21, 4, 17, 10, 25, 8, 14, 2,
    19, 5, 22, 11, 7, 18, 3, 15, 9, 24,
    6, 12, 4, 20, 8, 16, 1, 23, 10, 17,
    5, 13, 7, 25, 2, 19, 11, 6, 21, 4,
    14, 9, 22, 3, 18, 8, 15, 12, 24, 7,
    16, 1, 20, 5, 23, 10, 17, 6, 13, 4,
    25, 9, 19, 2, 14, 8, 21, 11, 18, 3,
    15, 7, 24, 12, 5, 22, 1, 16, 9, 20,
    4, 23, 6, 17, 10, 25, 8, 13, 2, 19,
    11, 21, 7, 14, 3, 18, 5, 24, 9, 15,
    12, 22, 1, 16, 6, 20, 4, 23, 8, 17,
    10, 25, 2, 19, 7, 13, 11, 21, 5, 14,
    3, 18, 9, 24, 6, 15, 12, 22, 1, 16,
    4, 20, 8, 23, 7, 17, 10, 25, 2, 19,
    11, 13, 5, 21, 9, 14, 3, 18, 6, 24,
    1, 15, 4, 22, 8, 16, 7, 20, 10, 23,
    2, 17, 5, 25, 9, 19, 3, 13, 6, 21,
    11, 14, 1, 18, 4, 24, 8, 15, 7, 22,
    2, 16, 5, 20, 9, 23, 3, 17, 6, 25,
    10, 19, 1, 13, 4, 21, 8, 14, 7, 18,
    2, 24, 5, 15, 9, 22, 3, 16, 6, 20,
    10, 23, 1, 17, 4, 25, 8, 19, 7, 13,
    2, 21, 5, 14, 9, 18, 3, 24, 6, 15,
    10, 22, 1, 16, 4, 20, 8, 23, 7, 17,
    2, 25, 5, 19, 9, 13, 3, 21, 6, 14,
    10, 18, 1, 24, 4, 15, 8, 22, 7, 16,
    2, 20, 5, 23, 9, 17, 3, 25, 6, 19,
    10, 13, 1, 21, 4, 14, 8, 18, 7, 24,
    2, 15, 5, 22, 9, 16, 3, 20, 6, 23,
    10, 17, 1, 25, 4, 19, 8, 13, 7, 21,
    2, 14, 5, 18, 9, 24, 3, 15, 6, 22,
    10, 16, 1, 20, 4, 23, 8, 17, 7, 25,
    2, 19, 5, 13, 9, 21, 3, 14, 6, 18,
    10, 24, 1, 15, 4, 22, 8, 16, 7, 20,
    2, 23, 5, 17, 9, 25, 3, 19, 6, 13,
    10, 21, 1, 14, 4, 18, 8, 24, 7, 15,
    2, 22, 5, 16, 9, 20, 3, 23, 6, 17,
    10, 25, 1, 19, 4, 13, 8, 21, 7, 14,
    2, 18, 5, 24, 9, 15, 3, 22, 6, 16,
    10, 20, 1, 23, 4, 17, 8, 25, 7, 19,
    2, 13, 5, 21, 9, 14, 3, 18, 6, 24,
    10, 15, 1, 22, 4, 16, 8, 20, 7, 23,
    2, 17, 5, 25, 9, 19, 3, 13, 6, 21,
    10, 14, 1, 18, 4, 24, 8, 15, 7, 22,
    
    // 대형 파일 (200-800) - 120개
    623, 456, 789, 234, 567, 345, 678, 123, 456, 789,
    712, 345, 678, 234, 567, 456, 789, 123, 345, 678,
    534, 267, 690, 423, 756, 289, 612, 345, 678, 401,
    724, 467, 750, 293, 616, 359, 682, 205, 548, 371,
    694, 427, 760, 283, 606, 329, 652, 475, 798, 321,
    644, 387, 710, 253, 576, 399, 722, 245, 568, 391,
    714, 437, 760, 283, 606, 329, 652, 475, 798, 321,
    644, 387, 710, 253, 576, 399, 722, 245, 568, 391,
    714, 437, 760, 283, 606, 329, 652, 475, 798, 321,
    644, 387, 710, 253, 576, 399, 722, 245, 568, 391,
    714, 437, 760, 283, 606, 329, 652, 475, 798, 321,
    644, 387, 710, 253, 576, 399, 722, 245, 568, 391
};

// 내림차순 정렬용 비교 함수
int cmp_desc(const void* a, const void* b) {
	return ((FileEntry*)b)->size - ((FileEntry*)a)->size;
}

// Greedy 분배 함수
void assign_files_greedy(int P, ProcessLoad buckets[P]) {
	FileEntry files[TOTAL_FILES];
	for (int i = 0; i < TOTAL_FILES; i++) {
    	files[i].index = i;
    	files[i].size = file_sizes[i];
	}

	qsort(files, TOTAL_FILES, sizeof(FileEntry), cmp_desc); // 내림차순 정렬

	for (int i = 0; i < P; i++) {
    	buckets[i].count = 0;
    	buckets[i].total_size = 0;
	}

	for (int i = 0; i < TOTAL_FILES; i++) {
    	// 가장 적은 작업량을 가진 프로세스 찾기
    	int min_idx = 0;
    	for (int j = 1; j < P; j++) {
        	if (buckets[j].total_size < buckets[min_idx].total_size)
            	min_idx = j;
    	}

    	// 배정
    	int proc = min_idx;
    	buckets[proc].indices[buckets[proc].count++] = files[i].index;
    	buckets[proc].total_size += files[i].size;
	}

	// 로그 출력
	printf("\n[파일 분배 결과 - Greedy 방식]\n");
	for (int i = 0; i < P; i++) {
    	printf("프로세스 %d: 총 작업량 = %d (파일 %d개)\n ", i, buckets[i].total_size, buckets[i].count);
    	for (int j = 0; j < buckets[i].count; j++) {
        	int idx = buckets[i].indices[j];
        	printf("file_%02d(%d) ", idx, file_sizes[idx]);
       	 
        	if ((j+1) % 6 == 0)
            	printf("\n  ");
    	}
    	printf("\n");
	}
}

// CPU 부하 시뮬레이션 함수
void run_cpu_for(int times) {
	volatile double dummy = 0;
	for (int i = 0; i < times * TIME_MULTIPLIER; i++) {
    	dummy += i * 0.000001;
	}
}

// BWT 단계 (지연 포함)
void apply_bwt(char* output, const char* input, int size) {
	run_cpu_for(5 * size);
	snprintf(output, 256, "bwt(%s)", input);
}

// MTF 단계 (지연 포함)
void apply_mtf(char* output, const char* input, int size) {
	run_cpu_for(3 * size);
	snprintf(output, 256, "mtf(%s)", input);
}

// RLE + Huffman 단계 (지연 포함)
void apply_rle(const char* input, int size) {
	run_cpu_for(2 * size);
}

// ── Process-only 모드 전용: 동적할당·락 없이 순차 처리 ──
void run_process_only(int P, int idx) {
	char buf1[256], buf2[256];
	for (int i = idx; i < TOTAL_FILES; i += P) {
    	int size = file_sizes[i];
    	apply_bwt(buf1, "content", size);
    	apply_mtf(buf2, buf1, size);
    	apply_rle(buf2, size);
	}
}

// greedy alg 적용된 process-only 모드
void run_process_only_optimized(ProcessLoad* my_bucket) {
	char buf1[256], buf2[256];
	for (int i = 0; i < my_bucket->count; i++) {
    	int idx = my_bucket->indices[i];
    	int size = file_sizes[idx];
    	apply_bwt(buf1, "content", size);
    	apply_mtf(buf2, buf1, size);
    	apply_rle(buf2, size);
	}
}

// ── Thread-only 모드 전용: 뮤텍스 없이 인덱스 분할 ──
typedef struct { int id, T; } ThreadArg;
void* thread_func_opt(void* _a) {
	ThreadArg * a = _a;
	char buf1[256], buf2[256];
	for (int i = a->id; i < TOTAL_FILES; i += a->T) {
    	int size = file_sizes[i];
    	apply_bwt(buf1, "content", size);
    	apply_mtf(buf2, buf1, size);
    	apply_rle(buf2, size);
	}
	return NULL;
}

void run_thread_only(int T) {
	pthread_t th[T];
	ThreadArg args[T];
	for (int t = 0; t < T; t++) {
    	args[t].id = t; args[t].T = T;
    	pthread_create(&th[t], NULL, thread_func_opt, &args[t]);
	}
	for (int t = 0; t < T; t++)
    	pthread_join(th[t], NULL);
}

// 작업 큐에 추가 (단계별 큐 사용)
void enqueue_task(Task* task) {
	pthread_mutex_lock(&queue_mutex);
	switch (task->stage) {
	case RAW:
    	raw_queue[raw_tail++] = task;
    	break;
	case BWT_DONE:
    	bwt_queue[bwt_tail++] = task;
    	break;
	case MTF_DONE:
    	mtf_queue[mtf_tail++] = task;
    	break;
	}
	pthread_cond_signal(&queue_not_empty);
	pthread_mutex_unlock(&queue_mutex);
}

// 우선순위가 가장 높은 작업을 큐에서 꺼냄
Task* dequeue_highest_priority_task() {
	pthread_mutex_lock(&queue_mutex);
	while (raw_head == raw_tail && bwt_head == bwt_tail && mtf_head == mtf_tail) {
    	pthread_cond_wait(&queue_not_empty, &queue_mutex);
	}
	Task* task = NULL;
	if (mtf_head < mtf_tail) {
    	task = mtf_queue[mtf_head++];
	} else if (bwt_head < bwt_tail) {
    	task = bwt_queue[bwt_head++];
	} else if (raw_head < raw_tail) {
    	task = raw_queue[raw_head++];
	}
	pthread_mutex_unlock(&queue_mutex);
	return task;
}

// 스레드가 수행할 작업 함수
void* worker_thread(void* arg) {
	while (1) {
    	Task* task = dequeue_highest_priority_task();
    	switch (task->stage) {
    	case RAW:
        	apply_bwt(task->data, task->data, task->size);
        	task->stage = BWT_DONE;
        	enqueue_task(task);
        	break;
    	case BWT_DONE:
        	apply_mtf(task->data, task->data, task->size);
        	task->stage = MTF_DONE;
        	enqueue_task(task);
        	break;
    	case MTF_DONE:
        	apply_rle(task->data, task->size);
        	pthread_mutex_lock(&complete_mutex);
        	completed_tasks++;
        	if (completed_tasks == task_target) {
            	pthread_cond_signal(&all_done);
        	}
        	pthread_mutex_unlock(&complete_mutex);
        	free(task->name);
        	free(task);
        	break;
    	}
	}
	return NULL;
}

// 압축 실행 함수: 각 프로세스마다 실행
void run_compressor(int thread_count, int proc_index, int total_proc) {
	if (thread_count <= 0) {
    	fprintf(stderr, "Invalid thread_count (must be ≥ 1).\n");
    	return;
	}
	pthread_t threads[thread_count];
	for (int i = 0; i < thread_count; i++) {
    	pthread_create(&threads[i], NULL, worker_thread, NULL);
	}
	int count = 0;
	for (int i = proc_index; i < TOTAL_FILES; i += total_proc) count++;
	task_target = count;
	for (int i = proc_index; i < TOTAL_FILES; i += total_proc) {
    	Task* task = malloc(sizeof(Task));
    	strncpy(task->data, "content", sizeof(task->data));
    	char name[32];
    	snprintf(name, sizeof(name), "file_%02d", i);
    	task->name = strdup(name);
    	task->stage = RAW;
    	task->size = file_sizes[i];
    	enqueue_task(task);
	}
	pthread_mutex_lock(&complete_mutex);
	while (completed_tasks < task_target)
    	pthread_cond_wait(&all_done, &complete_mutex);
	pthread_mutex_unlock(&complete_mutex);
}

// 메인 함수: 전체 프로세스를 생성하고 성능 측정
int main(int argc, char* argv[]) {
	if (argc != 3) {
    	fprintf(stderr, "Usage: %s <process_count> <thread_count>\n", argv[0]);
    	return 1;
	}
	int P = atoi(argv[1]);	// 자식 프로세스 수
	int T = atoi(argv[2]);	// 워커 스레드 수
	PerfMetrics metrics;

	// ──── 1) 순차(single) 모드 (C0) ───────────────────────────────
	if (P == 0 && T == 0) {
    	start_perf(&metrics);
    	char buf1[256], buf2[256];
    	for (int i = 0; i < TOTAL_FILES; i++) {
        	int size = file_sizes[i];
        	apply_bwt(buf1, "content", size);
        	apply_mtf(buf2, buf1, size);
        	apply_rle(buf2, size);
    	}
    	end_perf(&metrics, 0);
    	print_perf_summary(&metrics);
    	return 0;
	}

	// ──── 2) process-only 모드 (C1~C5) ─────────────────────────── ** 수정됨
	if (T == 0) {
    	start_perf(&metrics);
        for (int i = 0; i < P; i++) {
            pid_t pid = fork();
            if (pid == 0) {
                run_process_only(P, i);  // RR 방식 분배
                exit(0);
            }
        }

	end_perf(&metrics, P);
	print_perf_summary(&metrics);
	return 0;
}

	// ──── 3) thread-only 모드 (C6~C9) ────────────────────────────
	if (P == 0) {
    	start_perf(&metrics);
    	run_thread_only(T);
    	end_perf(&metrics, 0);
    	print_perf_summary(&metrics);
    	return 0;
	}

	// ──── 4) hybrid 모드 (C10~C14) ───────────────────────────────
	start_perf(&metrics);
	for (int i = 0; i < P; i++) {
    	pid_t pid = fork();
    	if (pid == 0) {
        	run_compressor(T, i, P);
        	exit(0);
    	}
	}
	end_perf(&metrics, P);
	print_perf_summary(&metrics);
	return 0;
}