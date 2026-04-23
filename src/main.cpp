#include <iostream>
#include "Btree/Btree.h"

using namespace std;

int main(){
    cout<<"Hello World"<<endl;
    int degree = 3;
    Btree::Btree *btree = new Btree::BtreeImpl(degree);
    btree->insert(1);
    vector<int> keys = {1, 2, 3, 4, 5};
    btree->build(keys);
    delete btree;
    return 0;
}