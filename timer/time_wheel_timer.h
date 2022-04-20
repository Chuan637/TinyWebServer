#include<time.h>
#include<netinet/in.h>
#include<stdio.h>

#define BUFFER_SIZE 64

class tw_timer;

/*绑定socket和定时器*/
struct client_data{
    struct sockaddr_in address;
    int sockfd;
    char buf[BUFFER_SIZE];
    tw_timer* timer;
};

/*定时器类*/
class tw_timer{
public:
    tw_timer(int rot, int ts)
    : next(nullptr), prev(nullptr), rotation(rot), time_slot(ts) {}
public:
    int rotation;   /*记录定时器在时间轮转多少圈后生效*/
    int time_slot;  /*记录定时器在时间轮上属于哪个槽*/
    void (*cb_func)(client_data*);  /*定时器回调函数*/
    client_data* user_data; /*客户数据 用于回调函数 不用关心其中的timer是什么*/
    tw_timer* next; /*指向下一个定时器*/
    tw_timer* prev; /*指向前一个定时器*/
};

class time_wheel{
public:
    time_wheel() : cur_slot(0)
    {
        for(int i = 0; i < N; ++i)
        {
            slots[i] = nullptr;
        }
    }
    ~time_wheel()
    {
        for(int i = 0; i < N; ++i)
        {
            tw_timer* tmp = slots[i];
            while(tmp)
            {
                slots[i] = tmp->next;
                delete tmp;
                tmp = slots[i];
            }
        }
    }
    
    /*根据定时值timeout创建一个定时器 并把它插入合适的槽中*/
    tw_timer* add_timer(int timeout)
    {
        if(timeout < 0)
        {
            return nullptr;
        }
        int ticks = 0;
        /*根据超时时间计算在多少个滴答后被触发*/
        if(timeout < SI)
        {
            ticks = 1;
        }
        else
        {
            ticks = timeout / SI;
        }
        /*计算待插入的定时器在时间轮转动多少圈后被触发*/
        int rotation = ticks / N;
        /*计算待插入的定时器应该被插入哪个槽*/
        int ts = (cur_slot + ticks % N) % N;
        tw_timer* timer = new tw_timer(rotation, ts);
        if(!slots[ts])
        {
            printf( "add timer, rotation is %d, ts is %d, cur_slot is %d\n",
                    rotation, ts, cur_slot );
            slots[ts] = timer;
        }
        else
        {
            timer->next = slots[ts];
            slots[ts]->prev = timer;
            slots[ts] = timer;
        }
        return timer;
    }

    void del_timer(tw_timer* timer)
    {
        if(!timer)
        {
            return;
        }
        int ts = timer->time_slot;
        if(timer == slots[ts])
        {
            slots[ts] = slots[ts]->next;
            if(slots[ts])
            {
                slots[ts]->prev = nullptr;
            }
            delete timer;
        }
        else
        {
            timer->prev->next = timer->next;
            if(timer->next)
            {
                timer->next->prev = timer->prev;
            }
            delete timer;
        }
    }

    /*SI时间到后 调用该函数 时间轮向前滚动一个槽的间隔*/
    void tick()
    {
        tw_timer* tmp = slots[cur_slot];
        printf("current slot is %d\n", cur_slot);
        while(tmp)
        {
            printf("tick the timer once\n");
            /*如果定时器的rotation大于0 则它在这一轮不起作用*/
            if(tmp->rotation > 0)
            {
                tmp->rotation--;
                tmp = tmp->next;
            }
            /*否则说明定时器到期 执行定时任务 然后删除*/
            else
            {
                tmp->cb_func(tmp->user_data);
                if(tmp == slots[cur_slot])
                {
                    printf("delete header in cur_slot\n");
                    slots[cur_slot] = tmp->next;
                    if(slots[cur_slot])
                    {
                        slots[cur_slot]->prev = nullptr;
                    }
                    delete tmp;
                    tmp = slots[cur_slot];
                }
                else
                {
                    tmp->prev->next = tmp->next;
                    if(tmp->next)
                    {
                        tmp->next->prev = tmp->prev;
                    }
                    tw_timer* tmp2 = tmp->next;
                    delete tmp;
                    tmp = tmp2;
                }
            }
        }
        cur_slot = ++cur_slot % N;   /*更新时间轮当前槽*/
    }
private:
    /*时间轮上槽的数目*/
    static const int N = 60;
    /*每1s时间轮转动一次 即槽间隔为1s*/
    static const int SI = 1;
    /*时间轮的槽 每个元素指向一个无序链表*/
    tw_timer* slots[N];
    int cur_slot;   /*时间轮当前槽*/
};