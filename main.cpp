#include <iostream>
#include <stdatomic.h>
#include <pthread.h>
#include <zconf.h>
#include <algorithm>
#include <vector>
#define NUM_THREADS 11

using namespace std;


int count = 0;

class test_and_set_lock{
private:
    std::atomic<bool> ready ;

    bool test_and_set(bool * flag){
        *flag = true;
        return *flag;
    }

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
    const int base =1;
    const int limit = 1;
    const int multiplier = 1;
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
    const double base = 0.000001;
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
};

class MCS_lock{

    std::atomic<Qnode*> tail ;
public:
    void acquire(Qnode &q){
//        Qnode* myself = new Qnode;
//        myself->waiting.store(true);
//        q->next.store(myself);
        q.next.store(nullptr);
        q.waiting.store(true,std::memory_order_relaxed);
//        Qnode *tmp = &q;
        Qnode* prev = tail.exchange(&q,std::memory_order_release);
////        atomic_compare_exchange_strong(tail,q,NULL);
//        tail.compare_exchange_strong(prev,q);
        if (prev != nullptr){
            prev->next.store(&q,std::memory_order_relaxed);
            while (q.waiting.load()){
//                sleep(0.2);
//                cout<<"acquire,while"<<pthread_self()<<endl;
            }

        }
//        cout<<"=";
//        cout<<"acquire"<<endl;
        atomic_thread_fence(std::memory_order_acquire);
        atomic_signal_fence(std::memory_order_acquire);
    }
    void release(Qnode &q){
//        atomic<Qnode* > tmp = *(q->next);
        Qnode* succ = (q.next).load(std::memory_order_acquire);
        if (succ == nullptr){
//            cout<<'1'<<endl;
            Qnode *tmp = &q;
            Qnode *_null = nullptr;
            if (tail.compare_exchange_strong(tmp, _null)){
//                q.waiting.store(false,std::memory_order_acquire);
//                cout<<"release"<<endl;
                return;
            }
            while(succ == nullptr){
//                sleep(0.2);
//                cout<<"release,while"<<pthread_self()<<endl;
                succ = q.next.load(std::memory_order_relaxed);
            }
        }
        succ->waiting.store(false);
//        cout<<"="<<endl;
//        atomic_thread_fence(std::memory_order_seq_cst);
//        cout<<"release"<<endl;
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
K42_MCS_lock locker;


void* phases2(void* args)
{

    extern int count;

    for (int i=0; i < 1000; i++ ){
//        Qnode Q ;

//        Q.next.store(nullptr);
//        Q.waiting.store(false);
        locker.acquire();
        count++;
        locker.release();
//        delete(Q);
    }

}
int main() {
    extern int count;
    pthread_t tids[NUM_THREADS];


    for(int i = 0; i < NUM_THREADS; ++i)
    {
        pthread_create(&tids[i], NULL, phases2, NULL);
    }
    for( int i=0; i < NUM_THREADS; i++ ) {
        pthread_join(tids[i],NULL);

    }
    cout<<count;

    return 0;
}