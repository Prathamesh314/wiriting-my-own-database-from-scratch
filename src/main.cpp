#include "Btree/Btree.h"
#include "Logger.h"

int main(){
    Logger::info("Hello World");
    int degree = 3;
    Btree::Btree *btree = new Btree::BtreeImpl(degree);
    btree->insert(1);
    btree->insert(2);
    btree->insert(3);
    btree->insert(4);
    //vector<int> keys = {1, 2, 3, 4, 5};
    //btree->build(keys);
    btree->printTree();

    TreeNode* keynode = btree->findKey(5);
    Logger::info(keynode);
    delete btree;
    return 0;
}