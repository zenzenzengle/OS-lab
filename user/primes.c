#include "kernel/types.h"
#include "user/user.h"

#define R 0
#define W 1

const uint INT_LEN = sizeof(int);

/**
 * @brief 读取左邻居的第一个数据
 * @param lp 左管道
 * @return 如果没有数据返回-1,有数据返回0
 */
int lp_first_data(int lp[2], int *dst)
{
  if (read(lp[R], dst, sizeof(int)) == sizeof(int)) {
    printf("prime %d\n", *dst);
    return 0;
  }
  return -1;
}

/**
 * @brief 读取左邻居的数据，将不能被first整除的写入右邻居
 * @param lp 左管道
 * @param rp 右管道
 * @param first 左管道的第一个数据
 */
void trans_data(int lp[2], int rp[2], int first)
{
  int data;
  // 从左管道读取数据
  while (read(lp[R], &data, sizeof(int)) == sizeof(int)) {
    // 将无法整除的数据传递入右管道
    if (data % first)
      write(rp[W], &data, sizeof(int));
  }
  close(lp[R]);
  close(rp[W]);
}

/**
 * @brief 找素数功能实现
 * @param lp 左管道
 */

void primes(int lp[2])
{
  close(lp[W]);
  int first;
  if (lp_first_data(lp, &first) == 0) {
    int p[2];
    pipe(p); // 当前的管道
    trans_data(lp, p, first);

    if (fork() == 0) {
      primes(p);    // 在子进程完成递归
    } else {
      close(p[R]);
      wait(0);
    }
  }
  exit(0);
}

int main(int argc, char const *argv[])
{
  int p[2];
  pipe(p);

  for (int i = 2; i <= 35; ++i) //写入初始数据
    write(p[W], &i, INT_LEN);

  if (fork() == 0) {
    primes(p);
  } else {
    close(p[W]);
    close(p[R]);
    wait(0);
  }

  exit(0);
}