#include <iostream>
#include <stdatomic.h>
#include <pthread.h>
#include <zconf.h>
#include <algorithm>
#include <vector>
using namespace std;
int  NUM_THREADS =5;
std::atomic<bool> ready (false);
int NUM_ITER = 1000;
int COUNT = 0;

class test_and_set_lock{
protected:
    std::atomic_flag flag = ATOMIC_FLAG_INIT;
public:
    void acquire(){
        while (flag.test_and_set());
    }
    void release(){
        flag.clear();
    }
};

class TSA_lock_backoff :public test_and_set_lock{
private:
    const int base =0.001;
    const int limit = 0.02;
    const int multiplier = 0.1;
//    std::atomic_flag flag = ATOMIC_FLAG_INIT;
public:
    void acquire(){
        int deley = base;
        while(flag.test_and_set()){
            sleep(deley);
            deley = min(deley*multiplier,limit);
        }
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
    }
    void release(){
        now_serving.fetch_add(1);
    }

};
class ticket_lock_backoff:public ticket_lock{
private:
    const double base = 0.0000000001;
public:
    void acquire(){
        int my_ticket = next_ticket.fetch_add(1);
        while(my_ticket != now_serving.load()){
            sleep(static_cast<unsigned int>(base * (my_ticket - now_serving.load())));
        }
    }
};
struct Qnode{
    atomic<Qnode*> next;
    std::atomic<bool> waiting;
    Qnode(){
        next = NULL;
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
        CLH_node* p = _thread_node_ptrs[self];
        p->waiting.store(true);
        CLH_node* pred = tail.exchange(p);
        while (pred->waiting.load());
        head.store(p);
        _thread_node_ptrs[self] = pred;
    }
    void release(){
        head.load()->waiting.store(false);
    }
};


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
        Qnode Q;
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
    }
}

K42_CLH_lock K42_CLH_locker;
void* K42_CLH_lock_func(void* args)
{
    int self = (long) args;
    for (int i=0; i < NUM_ITER; i++ ){
        K42_CLH_locker.acquire(self);
        COUNT++;
        K42_CLH_locker.release();
    }

}
void zhangyu(pthread_t *tids, void *func(void *arg)) {
    ready = false;
    COUNT = 0;
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
    cout<<"result:  "<<COUNT<<endl;
    cout<<"run time: "<<diff.count()<<endl;
}
int main(int argc,char **argv) {
    int n_ = 1000;
    for (int i = 0; i < argc; i++) {
        if (!strcmp(argv[i], "-t")) {
            NUM_THREADS = stoi(argv[i + 1]);
        } else if (!strcmp(argv[i], "-i")) {
            NUM_ITER = stoi(argv[i + 1]);
        }
    }
    cout<<NUM_THREADS<<endl;
    cout<<NUM_ITER<<endl;

    CLH_node initial_thread_nodes[NUM_THREADS];
    CLH_node* thread_node_ptrs[NUM_THREADS];
    for(int i = 0; i<NUM_THREADS;i++){
        thread_node_ptrs[i] = &initial_thread_nodes[i];
    }
    _thread_node_ptrs = thread_node_ptrs;

    pthread_t tids[NUM_THREADS];
    cout<<"test_and_set_lock_:"<<endl;
    zhangyu(tids,test_and_set_lock_func );
    cout<<"TSA_lock_backoff:"<<endl;
    zhangyu(tids,TSA_lock_backoff_func );
    cout<<"ticket_lock:"<<endl;
    zhangyu(tids,ticket_lock_func);
    cout<<"ticket_lock_backoff:"<<endl;
    zhangyu(tids,ticket_lock_backoff_func);
    cout<<"MCS_lock:"<<endl;
    zhangyu(tids,MCS_lock_func);
    cout<<"K42_MCS_lock:"<<endl;
    zhangyu(tids,K42_MCS_lock_func);
    cout<<"CLH_lock_:"<<endl;
    zhangyu(tids,CLH_lock_func);
    cout<<"K42_CLH_lock:"<<endl;
    zhangyu(tids,K42_CLH_lock_func);

}