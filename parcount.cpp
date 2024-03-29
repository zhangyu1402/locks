#include <iostream>
#include <stdatomic.h>
#include <pthread.h>
#include <zconf.h>
#include <algorithm>
#include <vector>
#include <atomic>
#include <cstring>
#include <chrono>
using namespace std;
int  NUM_THREADS =4;
std::atomic<bool> ready (false);
int NUM_ITER = 100000;
int COUNT = 0;
pthread_key_t pthread_key;
class test_and_set_lock{
protected:
    std::atomic_flag flag = ATOMIC_FLAG_INIT;
public:
    void acquire(){
        while (flag.test_and_set());
        atomic_thread_fence(std::memory_order_acquire);
        atomic_signal_fence(std::memory_order_acquire);
    }
    void release(){
        flag.clear();
    }
};

class TSA_lock_backoff :public test_and_set_lock{
private:
    int base =10000;
    int limit = 1000000;
    float multiplier = 2;
//    std::atomic_flag flag = ATOMIC_FLAG_INIT;
public:
    void set_TSA_lock_backoff(int a, int b, float c){
        this->base = a;
        this->limit = b;
        this->multiplier = c;
    }
    TSA_lock_backoff(){
    }
    void acquire(){
        int deley = base;
        while(flag.test_and_set()){
            auto time_first = std::chrono::high_resolution_clock::now();
            while ((std::chrono::high_resolution_clock::now() - time_first).count() < deley) ;
            deley = min((int)(deley*multiplier),limit);
        }
        atomic_thread_fence(std::memory_order_acquire);
        atomic_signal_fence(std::memory_order_acquire);
    }
};
class ticket_lock{
protected:
    std::atomic_int next_ticket;
    std::atomic_int now_serving;
public:
    void acquire(){
        int my_ticket = next_ticket.fetch_add(1);
        while(my_ticket != now_serving.load());
        atomic_thread_fence(std::memory_order_acquire);
        atomic_signal_fence(std::memory_order_acquire);
    }
    void release(){
        now_serving.fetch_add(1);
    }
};
class ticket_lock_backoff:public ticket_lock{
private:
    int base = 25;
public:
    void set_base(int a){
        this->base = a;
    }
    void acquire(){
        int my_ticket = next_ticket.fetch_add(1);
        while(my_ticket != now_serving.load()){
            auto time_first = std::chrono::high_resolution_clock::now();
            while ((std::chrono::high_resolution_clock::now() - time_first).count() < base);
        }
        atomic_thread_fence(std::memory_order_acquire);
        atomic_signal_fence(std::memory_order_acquire);

    }
};
struct Qnode{
    atomic<Qnode*> next;
    std::atomic<bool> waiting;
    Qnode(Qnode const &other){}
    Qnode(){
        next.store(nullptr);
        waiting.store(false);
    }
    Qnode(Qnode *next,bool waiting) {
        this->next.store(next);
        this->waiting.store(waiting);
    }

};

class MCS_lock{

    std::atomic<Qnode*> tail ;
public:
    void acquire(Qnode &q){
        q.next.store(nullptr);
        q.waiting.store(true,std::memory_order_relaxed);
        Qnode* prev = tail.exchange(&q,std::memory_order_release);
        if (prev != nullptr){
            prev->next.store(&q,std::memory_order_relaxed);
            while (q.waiting.load()){
            }
        }
        atomic_thread_fence(std::memory_order_acquire);
        atomic_signal_fence(std::memory_order_acquire);
    }
    void release(Qnode &q){
        Qnode* succ = (q.next).load(std::memory_order_acquire);
        if (succ == nullptr){
            Qnode *tmp = &q;
            Qnode *_null = nullptr;
            if (tail.compare_exchange_strong(tmp, _null)){
                return;
            }
            while(succ == nullptr){
                succ = q.next.load(std::memory_order_relaxed);
            }
        }
        succ->waiting.store(false);
    }
};


struct K42_Qnode{
    atomic<K42_Qnode*> tail;
    atomic<K42_Qnode*> next;
    K42_Qnode(){
        tail.store(nullptr);
        next.store(nullptr);
    };
    K42_Qnode(K42_Qnode* a,K42_Qnode* b){
        tail.store(a);
        next.store(b);
    };
};
const K42_Qnode* waiting = new K42_Qnode;
class K42_MCS_lock{
    K42_Qnode q = {nullptr,nullptr};
public:
    void acquire(){
        while (true){
           K42_Qnode* prev = q.tail.load();
           if (prev == nullptr) {
               K42_Qnode *tmp = nullptr;
               if( ((q.tail).compare_exchange_strong(tmp, &q))) {
                   break;
               }
           }
           else{
               K42_Qnode n  = {(K42_Qnode*)waiting, nullptr};
               if ((q.tail).compare_exchange_strong(prev,&n)){
                   prev->next.store(&n);
                   while (n.tail.load() == waiting);
                   K42_Qnode* succ = n.next.load();
                   if (succ == nullptr){
                       q.next.store(nullptr);
                       K42_Qnode* tmp_n = &n;
                       if(!(q.tail).compare_exchange_strong(tmp_n,&q)){
                           while(succ == nullptr){
                               succ = n.next.load();
                           }
                           q.next.store(succ);
                       }
                       break;
                   }else{
                       q.next.store(succ);
                       break;
                   }
               }
           }
       }
        atomic_thread_fence(std::memory_order_acquire);
        atomic_signal_fence(std::memory_order_acquire);
    }

    void release(){
        K42_Qnode* succ = q.next.load();
        if(succ == nullptr){
            K42_Qnode *tmp_q = &q;
            K42_Qnode *_null = nullptr;
            if((q.tail).compare_exchange_strong(tmp_q, _null))
                return;
            while(succ == nullptr){
                succ = q.next.load();
            }
        }
        succ->tail.store(nullptr);
    }
};

class CLH_lock{
    Qnode dummy = {nullptr, false};
    atomic<Qnode*> tail;
public:
    CLH_lock(){
        this->tail.store(&dummy);
    }
    void acquire(Qnode* p){
        p->waiting.store(true);
        p->next = tail.exchange(p);
        Qnode* pred = p->next;
        while(pred->waiting.load());
        atomic_thread_fence(std::memory_order_acquire);
        atomic_signal_fence(std::memory_order_acquire);
    }
    void release(Qnode** pp){
        Qnode *pred = (*pp)->next;
        (*pp)->waiting.store(false);
        *pp = pred;
    }
};

struct CLH_node{
    atomic<bool> waiting;
    CLH_node(bool wait){
        this->waiting.store(wait);
    }
    CLH_node(){
        this->waiting.store(false);
    }
//    CLH_node(CLH_node const &other){
//        this->waiting.store(other.waiting.load()) ;
//    }
};
CLH_node **_thread_node_ptrs;

class K42_CLH_lock{
    CLH_node dummy = {false};
    atomic<CLH_node*> tail;
    atomic<CLH_node*> head;

public:
    K42_CLH_lock(){
        this->tail.store(&dummy);
    }
    void acquire(int self){
//        CLH_node* _p = _thread_node_ptrs[self];
        CLH_node** p_ptr = (CLH_node**)pthread_getspecific(pthread_key);
        CLH_node* p = *p_ptr;
        p->waiting.store(true);
        CLH_node* pred = tail.exchange(p);
        while (pred->waiting.load());
        head.store(p);
//        _thread_node_ptrs[self] = pred;
        *p_ptr = pred;
        atomic_thread_fence(std::memory_order_acquire);
        atomic_signal_fence(std::memory_order_acquire);

    }
    void release(){
        head.load()->waiting.store(false);
    }
};
std::mutex _mutex;

void *mutex_func1(void* args){
    while (!ready);
    for (int i=0; i < NUM_ITER; i++ ){
        _mutex.lock();
        COUNT++;
        _mutex.unlock();
    }
}

test_and_set_lock TASlock;
void* test_and_set_lock_func(void* args){
    while (!ready);
    for (int i=0; i < NUM_ITER; i++ ){
        TASlock.acquire();
        COUNT++;
        TASlock.release();
    }
}

TSA_lock_backoff TAS_back_off_lock;
void* TSA_lock_backoff_func(void* args){

    while (!ready);
    for (int i=0; i < NUM_ITER; i++ ){
        TAS_back_off_lock.acquire();
        COUNT++;
        TAS_back_off_lock.release();
    }
}

ticket_lock ticket_locker;
void* ticket_lock_func(void* args){
    while (!ready);
    for (int i=0; i < NUM_ITER; i++ ){
        ticket_locker.acquire();
        COUNT++;
        ticket_locker.release();
    }
}

ticket_lock_backoff ticket_locker_backoff;
void *ticket_lock_backoff_func(void* args){
    while (!ready);
    for (int i=0; i < NUM_ITER; i++ ){
        ticket_locker_backoff.acquire();
        COUNT++;
        ticket_locker_backoff.release();
    }
}

MCS_lock MCS_locker;
void* MCS_lock_func(void * args){
    while (!ready);
    for (int i=0; i < NUM_ITER; i++ ){
        Qnode Q ;
        MCS_locker.acquire(Q);
        COUNT++;
        MCS_locker.release(Q);
    }
}

K42_MCS_lock K42_MCS_locker;
void* K42_MCS_lock_func(void * args){
    while (!ready);
    for (int i=0; i < NUM_ITER; i++ ){
        K42_MCS_locker.acquire();
        COUNT++;
        K42_MCS_locker.release();
    }
}

CLH_lock CLH_locker;
void* CLH_lock_func(void* args){
    while (!ready);
    for (int i=0; i < NUM_ITER; i++ ){
        Qnode *q = new Qnode;
        CLH_locker.acquire(q);
        COUNT++;
        CLH_locker.release(&q);
//        delete  q;
    }
}

K42_CLH_lock K42_CLH_locker ;
void* K42_CLH_lock_func(void* args)
{
    long self = (long) args;
    CLH_node* tmp = (_thread_node_ptrs[self]);
    pthread_setspecific(pthread_key,&(_thread_node_ptrs[self]));

    for (int i=0; i < NUM_ITER; i++ ){
        K42_CLH_locker.acquire((int)self);
        COUNT++;
//        if (COUNT%1000 == 0)
//            cout<<COUNT<<endl;
        K42_CLH_locker.release();

    }

}
void zhangyu(pthread_t *tids, void *func(void *arg)) {
    ready = false;
    COUNT = 0;
    pthread_key_create(&pthread_key, nullptr);
    for (int i = 0; i < NUM_THREADS; ++i) {
        pthread_create(&tids[i], NULL, func, (void *) i);
    }
    ready = true;
    auto time1 = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_join(tids[i], NULL);
    }
    auto time2 = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::milli> diff = time2 - time1;
//    cout<<"result:  "<<COUNT<<endl;
//    cout<<"run time: "<<diff.count()<<endl;
    cout<<(NUM_ITER)/diff.count()<<endl;
}
int main(int argc,char **argv) {
//    int n_ = 1000;
    for (int i = 0; i < argc; i++) {
        if (!strcmp(argv[i], "-t")) {
            NUM_THREADS = stoi(argv[i + 1]);
        } else if (!strcmp(argv[i], "-i")) {
            NUM_ITER = stoi(argv[i + 1]);
        }
    }
//    cout<<NUM_THREADS<<endl;
//    cout<<NUM_ITER<<endl;

    CLH_node initial_thread_nodes[NUM_THREADS];
    CLH_node* thread_node_ptrs[NUM_THREADS];
    for(int i = 0; i<NUM_THREADS;i++){
        thread_node_ptrs[i] = &initial_thread_nodes[i];
    }
    _thread_node_ptrs = thread_node_ptrs;
//    for (int i = 0;i < 10;i++){
//        Qnode q ;
//        cout<<"test";
//    }
    pthread_t tids[NUM_THREADS];
//    cout<<"C++ mutex:"<<endl;
//    zhangyu(tids,mutex_func1);
//    cout<<"test_and_set_lock_:"<<endl;
//    zhangyu(tids,test_and_set_lock_func );
//    cout<<"TSA_lock_backoff:"<<endl;
//    zhangyu(tids,TSA_lock_backoff_func );
//    cout<<"ticket_lock:"<<endl;
//    zhangyu(tids,ticket_lock_func);
//    cout<<"ticket_lock_backoff:"<<endl;
//    zhangyu(tids,ticket_lock_backoff_func);
//    cout<<"MCS_lock:"<<endl;
//    zhangyu(tids,MCS_lock_func);
//    cout<<"K42_MCS_lock:"<<endl;
//    zhangyu(tids,K42_MCS_lock_func);
//    cout<<"CLH_lock_:"<<endl;
//    zhangyu(tids,CLH_lock_func);
//    cout<<"K42_CLH_lock:"<<endl;
//    zhangyu(tids,K42_CLH_lock_func);



    for (int i = 10;i<10000000;i=i+10){
        ticket_locker_backoff.set_base(i);
        zhangyu(tids,ticket_lock_backoff_func);
    }

}