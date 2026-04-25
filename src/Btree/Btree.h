#pragma once
#include <iostream>
#include <vector>

/*
Here is ASCII representation of Btree to understand properly

[ K1 | K2 | K3 ]
  /     |     |    \
 C0    C1    C2    C3

*/

struct TreeNode{
    std::vector<int> keys;
    std::vector<TreeNode*> children;
    int degree;
    bool isLeaf;

    TreeNode(int _degree) {
        this->degree = _degree;
        this->isLeaf = true;
    }
};

namespace Btree {
    class Btree {
        public:
            virtual ~Btree() = default;
            virtual void insert(int key) = 0;
            virtual void build(std::vector<int>& keys) = 0;
    };

    class BtreeImpl : public Btree {
        private:
            int degree;
            TreeNode *root;
        public:
            BtreeImpl( int degree) {
                this->degree = degree;
                this->root = new TreeNode(degree); 
                std::cout << "BtreeImpl constructor called!" << std::endl;
            }

            void insert(int key) override {
                std::cout << "Inserting " << key << " in btree" << std::endl;
            }

            void build(std::vector<int>& keys) override {
                std::cout << "Building btree from keys" << std::endl;
                for (auto it : keys) {
                    std::cout << "Key: " << it << std::endl;
                }
            }

            ~BtreeImpl() override {
                std::cout << "BtreeImpl destructor called!" << std::endl;
            }
    };
}
