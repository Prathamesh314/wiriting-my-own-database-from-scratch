#include "Btree/Btree.h"

int main(){
    int degree = 5;
    Btree::Btree* btree = new Btree::BtreeImpl(degree);
    for (int k : {10, 20, 30, 40, 50, 60, 70, 80, 90}) btree->insert(k);
    btree->printTree();

    btree->deleteKey(30);   // leaf delete (Case 1)
    btree->printTree();

    btree->deleteKey(50);   // probably internal delete (Case 2)
    btree->printTree();

    btree->deleteKey(70);   // may trigger borrow / merge
    btree->printTree();

    return 0;
}