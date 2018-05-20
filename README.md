# 内存文件系统
3-guanxiux created by GitHub Classroom
## 内存设置构成: 
  将 mmap 到的完整的内存划分为 blocknr 个块: 
  
  + 将大小为 size 的内存的头指针设为 第 0 号 mem 块的头指针. 第 0 号块存储这个文件系统的信息, 有 blocksize, blocknr, 文件链表的头节点 root, 到文件结点地址空间(存储文件链表的结点的 mem 块)的指针 FileNodeAddressSpace, 到链表结点地址空间(存储有关 mem 块使用情况的链表结点的 mem 块)的指针 LinkListAddressSpace, 可用 mem 块链表的头指针 aMemHead, 可用 mem 块链表的尾指针 aMemHeap. 这个块的大小通过储存的各个变量的大小和确定. 各个变量在这个块上的存储位置姑且当作某种协议, 用全局变量确定.
  
  + 第 1 号块到 blocknr - 3 号块依次紧挨前一个块, 大小是 blocksize, 用作存储文件内容的块, 这些块内不含任何指针.
  
  + 第 blocknr - 2 号块是 LinkListAddressSpace, 保险起见地址与前一个块的尾部相隔 128, 存储着记录 mem 块的使用情况的链表, 在其中维护的链表有, 各个文件内容所在的块指针的链表(头指针尾指针存储在每一个文件的 filenode 上), 可用的 mem 块的队列(头指针 aMemHead ,尾指针 aMemHeap, 存储的是是前述 mem\[1\] 到 mem\[blocknr -3\] 的指针), 这个块中空余的未被使用的链表结点栈(头指针存储在这个块头 8 个字节上, 记为 pointer_to_available). 因为上述的链表都可以使用同一种结构体如下作为结点, 所以将它们存在同一个块中.为确保结点数目足够, 块的大小暂定为这种结构体的大小与3 * (block - 2) 的乘积.
  ```
  struct LinkList {
    char *mem_block;
    struct LinkList *prev;
    struct LinkList * next;
};
```
  + 第 blocknr - 1 号块是 FileNodeAddressSpace, 地址与前一个块的尾部相隔 128, 存储记录文件信息的链表, 在其中维护的链表有, 储存内存中存在的文件的信息的链表(头指针为 root), 这个块中空余未被使用的链表结点栈(头指针存储在这个块头 8 个字节上, 记为 pointer_to_available). 块的大小是 filenode 大小加 256 加 stat 结构体大小的和与(blocknr - 2)的乘积. 每个 filenode 结构体后都设置存储文件名和 stat 结构体的空间.
  ```
  struct filenode {
    char *filename;
    struct LinkList *content;
    struct LinkList *heap;
    struct stat *st;
    struct filenode *next;
    struct filenode *prev;
};
```
## 目标实现情况
### 设置的全局变量只有 blocksize, blocknr, 第 0 号块内的各信息存储位置偏移量和某些操作的辅助变量. 读取 filenode 链表的头指针, FileNodeAddressSpace, LinkListAddressSpace 等关键信息时都是通过读取内存文件系统内的块完成的.
### mmap()之后 mem\[1\] 到 mem\[blocknr - 3\] 的块指针都进行了 munmap 操作, 后续取用释放时再针对这些进行操作.
### 扩展方面
+ 保证至少 size 达到约 2000 B 以上(足够存储FileNodeAddressSpace, LinkListAddressSpace 和 mem\[0])的情况下, size 和 MEMNUM 可以任意设置.(但可用的 blocknr 比 MEMNUM 少一些, 因为通常 FileNodeAddressSpace, LinkListAddressSpace 会占据多个块)
+ 另外针对读写操作进行了一定程度的优化, 在向文件读写内容, 执行一次 oshfs_write 或 oshfs_read 函数时, 若是承接上一次的读写, 在同一个块或者下一个块内进行读写, 将不会再一次遍历文件的 mem 块而直接在同一个, 或者后继块上读写. 并且由于可用块和可用结点的队列和栈的实现, 分配一个结点和内存块的时间复杂度都是 O(1), 一定程度上提升了读写性能. 测试写简单的大文件时速度能达到 400M / s 以上.
+ 执行过程中适时地改写了基本的一些 stat 结构体里的变量.
+ 存储文件内容的块里没有存储指针, 文件内容的后继关系都存储在 LinkListAddressSpace 中

