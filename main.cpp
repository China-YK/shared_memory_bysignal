#include <cstdio>
#include <cstdlib>
#include <unistd.h>
#include <sys/shm.h>
#include <cstring>
#include <wait.h>

#include <sys/sem.h>

typedef struct{
    int id;
    char name[128];
    int age;
    bool sex;
}Student,*PStudent;

//标准写法应该用两个信号量(信号量的个数应该和通信方向匹配，父->子，子->父)
void test_signal1(){

    //10-21搞了一晚上，test_signal2,test_signal3，都会发生时态竞争
    //无论如何都会有几率出现子进程全部执行完了，父进程都还没执行第一行指令，导致子进程读到脏数据
    //结果我们只需要在fork之前创建信号量集就能保证父与子进程都指向同一信号集，P操作能成功阻塞！！！
    int sem_id = semget(ftok(".",2), 2, IPC_CREAT | 0666);
    //创建信号量集
    //第一个参数生成唯一key，用于标识该信号量集
    //第二个参数表示这个集合里面信号量的个数
    //第三个参数表示若信号量不存在则创建，若存在则返回已有的 ID。
    semctl(sem_id,0,SETVAL,0);
    //设置信号集里面的第一个信号(从0开始计数)的值为0
    semctl(sem_id,1,SETVAL,0);
    //设置信号集里面的第二个信号(从0开始计数)的值为0
    pid_t pid = fork();
    if (pid > 0){
        int shm_id = shmget(ftok(".",1),sizeof(Student),IPC_CREAT | 0666);
        if (shm_id == -1){//标识符为-1就表示创建失败
            std::printf("%s(%d)%s shmget failed!\n",__FILE__,__LINE__,__FUNCTION__);
            std::exit(-1);
        }
        PStudent pstu = (PStudent)shmat(shm_id,nullptr,0);
        pstu->id = 2025103620;
        std::strcpy(pstu->name,"YK Emperor!");
        pstu->age = 18;
        pstu->sex = true;

        sembuf sop = {
            .sem_num = 0,
            .sem_op = 1
        };
        semop(sem_id,&sop,1);//执行V操作
        //现在sop里面的值为.sem_num = 0，.sem_op = 1，表示使用第一个信号量，操作值为1(V操作)
        //第三个参数表示sop的数量，因为sop还可以是一个数组
        // struct sembuf sops[2];
        // sops[0].sem_num=0;
        // sops[0].sem_op=-1;
        // sops[1].sem_num=1;
        // sops[1].sem_op=-1;
        // semop(sem_id,sops,2);
        //比如我吃个蛋糕，蛋糕数量减一就是P操作对应上面的sops[0].sem_op=-1,同时屎的数量加一就是V操作对应上面的sops[1].sem_op=1

        sop.sem_num = 1;
        sop.sem_op = -1;
        semop(sem_id,&sop,1);//执行P操作

        shmdt(pstu);
        shmctl(shm_id,IPC_RMID,nullptr);

        //删除信号量
        semctl(sem_id,0,IPC_RMID);
        //也会存在引用计数，等所有持有这个信号量集的进程都执行删除操作，才会真正的删除这个信号量集

    }else if (pid == 0){
        // int sem_id = semget(ftok(".",2),2,IPC_CREAT);
        // sleep(1);
        //别重复设置信号量呀，父进程那边初始化为0就行了，我们都是操作同一信号量，不需要重复初始化，会出bug
        // semctl(sem_id,0,SETVAL,0);
        // semctl(sem_id,1,SETVAL,0);
        int shm_id = shmget(ftok(".",1),sizeof(Student),IPC_CREAT|0666);
        if (shm_id == -1){
            fprintf(stderr,"shmget failed!\n");
            exit(-1);
        }
        PStudent pstu =  (PStudent)shmat(shm_id,nullptr,0);
        sembuf sop = {
            .sem_num = 0,
            .sem_op = -1
        };
        semop(sem_id,&sop,1);//执行P操作

        fprintf(stdout,"GetStudent is:%d %s %d %s",pstu->id,pstu->name,pstu->age,pstu->sex == true?"male":"female");

        sop.sem_num = 1;
        sop.sem_op = 1;
        semop(sem_id,&sop,1);//执行V操作
        shmdt(pstu);
        semctl(sem_id,0,IPC_RMID);
    }else{
        fprintf(stderr,"fork failed!\n");
    }
}





//看看情况2，这个时候我们只设置一个信号量来尝试进行同步操作
//这里会发生奇怪的地方，有时候运行没问题，子进程可以正确输出内容。但有时候子进程又无法正确输出内容了
//我们讲一下子进程正确输出内容的情况(属于是理想情况)
//父进程在完成共享内存内容填充之后执行V操作，告诉子进程我已经准备好了
//子进程执行P操作，发现信号量>=1因此被唤醒了，然后正确得到共享内存里面的内容
//接着子进程执行V操作，信号量重回到1了，然后父进程的P操作也不会再阻塞，两边都顺利关闭信号集

//但是一切都不是那么顺利，所以我们讲一下子进程无法正确输出内容的情况(多数情况)
//其实是子进程先执行信号量集创建------>一直到P操作哪里
//这里补充一个知识点，就是当一个信号量集合目前只被一个进程拿到，那么这个P操作会返回错误，同时程序也会继续往下执行
//所以这里为什么子进程的P没有阻塞，导致子进程读到了一块还没填充内容的共享内存
//也就是说当你子进程都执行完了，父进程第一行语句都还没来得及执行
void test_signal2(){
    pid_t pid = fork();
    if (pid > 0){
        int sem_id = semget(ftok(".",2),1,IPC_CREAT);
        //创建信号量集
        //第一个参数生成唯一key，用于标识该信号量集
        //第二个参数表示这个集合里面信号量的个数
        //第三个参数表示若信号量不存在则创建，若存在则返回已有的 ID。
        semctl(sem_id,0,SETVAL,0);
        int shm_id = shmget(ftok(".",1),sizeof(Student),IPC_CREAT | 0666);
        if (shm_id == -1){//标识符为-1就表示创建失败
            std::printf("%s(%d)%s shmget failed!\n",__FILE__,__LINE__,__FUNCTION__);
            std::exit(-1);
        }
        PStudent pstu = (PStudent)shmat(shm_id,nullptr,0);
        pstu->id = 2025103620;
        std::strcpy(pstu->name,"YK Emperor!");
        pstu->age = 18;
        pstu->sex = true;

        sembuf sop = {
            .sem_num = 0,
            .sem_op = 1
        };
        semop(sem_id,&sop,1);//执行V操作

        sop.sem_num = 0;
        sop.sem_op = -1;
        semop(sem_id,&sop,1);//执行P操作

        shmdt(pstu);
        shmctl(shm_id,IPC_RMID,nullptr);

        //删除信号量
        semctl(sem_id,0,IPC_RMID);

    }else if (pid == 0){
        int sem_id = semget(ftok(".",2),1,IPC_CREAT);
        semctl(sem_id,0,SETVAL,0);
        int shm_id = shmget(ftok(".",1),sizeof(Student),IPC_CREAT|0666);
        if (shm_id == -1){
            fprintf(stderr,"shmget failed!\n");
            exit(-1);
        }
        PStudent pstu =  (PStudent)shmat(shm_id,nullptr,0);
        sembuf sop = {
            .sem_num = 0,
            .sem_op = -1
        };
        semop(sem_id,&sop,1);//执行P操作

        fprintf(stdout,"GetStudent is:%d %s %d %s",pstu->id,pstu->name,pstu->age,pstu->sex == true?"male":"female");
        sop.sem_num = 0;
        sop.sem_op = 1;
        semop(sem_id,&sop,1);//执行V操作
        shmdt(pstu);
        semctl(sem_id,0,IPC_RMID);
    }else{
        fprintf(stderr,"fork failed!\n");
    }
}

//这个就是上面的P失效的情况(当一个信号量集合目前只被一个进程拿到)
void test_signal3(){
    pid_t pid = fork();
    if (pid > 0){
        int sem_id = semget(ftok(".",2),2,IPC_CREAT);
        //创建信号量集
        //第一个参数生成唯一key，用于标识该信号量集
        //第二个参数表示这个集合里面信号量的个数
        //第三个参数表示若信号量不存在则创建，若存在则返回已有的 ID。
        semctl(sem_id,0,SETVAL,0);
        semctl(sem_id,1,SETVAL,0);
        int shm_id = shmget(ftok(".",1),sizeof(Student),IPC_CREAT | 0666);
        if (shm_id == -1){//标识符为-1就表示创建失败
            std::printf("%s(%d)%s shmget failed!\n",__FILE__,__LINE__,__FUNCTION__);
            std::exit(-1);
        }
        PStudent pstu = (PStudent)shmat(shm_id,nullptr,0);
        pstu->id = 2025103620;
        std::strcpy(pstu->name,"YK Emperor!");
        pstu->age = 18;
        pstu->sex = true;

        // sembuf sop = {
        //     .sem_num = 0,
        //     .sem_op = 1
        // };
        // semop(sem_id,&sop,1);//执行V操作
        //
        // sleep(1);
        // // sop.sem_num = 1;
        // sop.sem_num = 0;
        // sop.sem_op = -1;
        // semop(sem_id,&sop,1);//执行P操作

        shmdt(pstu);
        shmctl(shm_id,IPC_RMID,nullptr);

        //删除信号量
        // semctl(sem_id,0,IPC_RMID);
        // semctl(sem_id,1,IPC_RMID);

    }else if (pid == 0){
        int sem_id = semget(ftok(".",2),2,IPC_CREAT);
        int shm_id = shmget(ftok(".",1),sizeof(Student),IPC_CREAT|0666);
        if (shm_id == -1){
            fprintf(stderr,"shmget failed!\n");
            exit(-1);
        }
        PStudent pstu =  (PStudent)shmat(shm_id,nullptr,0);
        sembuf sop = {
            .sem_num = 0,
            .sem_op = -1
        };
        semop(sem_id,&sop,1);//执行P操作

        fprintf(stdout,"GetStudent is:%d %s %d %s",pstu->id,pstu->name,pstu->age,pstu->sex == true?"male":"female");
        sop.sem_num = 0;
        sop.sem_op = 1;
        semop(sem_id,&sop,1);//执行V操作
        shmdt(pstu);
        semctl(sem_id,0,IPC_RMID);
        semctl(sem_id,1,IPC_RMID);
    }else{
        fprintf(stderr,"fork failed!\n");
    }
}
int main(){
    test_signal1();
    return 0;
}