
#include "kernel/types.h"
#include "user/user.h"

#define R 0 //管道的读取端
#define W 1 //管道的写入端

int main(int argc, char const *argv[]) {
    char buf = 'o'; //用于传送的字节

    int cp[2]; //子进程->父进程
    int pc[2]; //父进程->子进程
    pipe(cp);
    pipe(pc);

    int pid = fork();
    int exit_status = 0;
    
    if (pid < 0) { //  fork失败的情况，直接退出
        fprintf(2, "fork() error!\n");
        close(cp[R]);
        close(cp[W]);
        close(pc[R]);
        close(pc[W]);
        exit(1);

    } else if (pid == 0) { //子进程先读后写
        
        close(pc[W]);//父到子管道的写入关闭
        close(cp[R]);//子到父管道的读取关闭 

        if (read(pc[R], &buf, sizeof(char)) == sizeof(char)) {
            fprintf(1, "%d: received ping\n", getpid());
        } else {
            fprintf(2, "子进程读不到！\n");
            exit_status = 1; //标记出错
        }

        if (write(cp[W], &buf, sizeof(char)) != sizeof(char)) {
            fprintf(2, "子进程写不进！\n");
            exit_status = 1;
        }


        close(pc[R]);
        close(cp[W]);

        exit(exit_status);

    } else { //父进程先写后读
        close(pc[R]);
        close(cp[W]);

        if (write(pc[W], &buf, sizeof(char)) != sizeof(char)) {
            fprintf(2, "父进程写不进！\n");
            exit_status = 1;
        }

        if (read(cp[R], &buf, sizeof(char)) == sizeof(char)) {
            fprintf(1, "%d: received pong\n", getpid());
        } else {
            fprintf(2, "父进程读不到！\n");
            exit_status = 1; //标记出错
        }

       
    

        close(pc[W]);
        close(cp[R]);

        exit(exit_status);
    }
}

