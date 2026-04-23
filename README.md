# wiriting-my-own-database-from-scratch

let's see how databases works?
 As far as i know

 1. the data is stored in a disk on a machine/vm.
 2. when we run a query in sql, it retrives that data from disk and loads it into ram/main memory
 3. the disk has blocks, and a block is represented as (track, sector)
 4. a disk is mutliple cocentric circles called tracks and a disk is divided into multiple parts called sectors
 5. to fetch a data we need the block address, which is (track, sector)
 6. now, if we load whole disk into memory to search the query result, then it will be very inefficient and heavy operation on machine.
 7. so we need to fetch only the required blocks from the disk to the memory.
 8. how are we going to fetch the required blocks from the disk to the memory?
    a. here comes the b - tree, we will store a data into bytes in a disk and we will have an index table which will point towards that data in disk.


```text
          [ B-Tree Index Structure ]
          --------------------------
                  [  Root  ]
                 /    |     \
          [ Node ] [ Node ] [ Node ]  <-- (Internal Nodes)
           /  \      /  \      /  \
          [L1][L2]  [L3][L4]  [L5][L6] <-- (Leaf Nodes: Key + Block Address)
           |   |     |   |     |   |
    =======|===|=====|===|=====|===|=============================
           v   v     v   v     v   v      (Disk Interface)
    +-----------------------------------------------------------+
    |                      HARD DISK DRIVE                      |
    |  +---------+   +---------+   +---------+   +---------+    |
    |  | Block A |   | Block B |   | Block C |   | Block D |    |
    |  | (Track, |   | (Track, |   | (Track, |   | (Track, |    |
    |  | Sector) |   | Sector) |   | Sector) |   | Sector) |    |
    |  +---------+   +---------+   +---------+   +---------+    |
    +-----------------------------------------------------------+
```
