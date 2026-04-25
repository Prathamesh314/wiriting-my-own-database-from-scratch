#pragma once

#include <algorithm>
#include <iostream>
#include <string>
#include <stdexcept>
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

    TreeNode(int _degree, std::vector<int> _keys, std::vector<TreeNode*> _children, bool _isLeaf){
        this->degree = _degree;
        this->keys = _keys;
        this->children = _children;
        this->isLeaf = _isLeaf;
    }

    bool isFull() const {
        return int(keys.size()) == degree - 1;
    }

    // Precondition: this node is NOT full. Caller must guarantee it.
    void insertNonFull(int key) {
        if (isLeaf) {
            auto pos = std::upper_bound(keys.begin(), keys.end(), key);
            keys.insert(pos, key);
            return;
        }

        int i = std::upper_bound(keys.begin(), keys.end(), key) - keys.begin();

        // Preemptive split: if the child we're about to descend into is full,
        // split it first. This guarantees the recursive call's precondition.
        if (children[i]->isFull()) {
            splitChild(i);
            // Median was promoted into keys[i]. Pick which half the new key belongs to.
            if (key > keys[i]) {
                i++;
            }
        }

        children[i]->insertNonFull(key);
    }

    // Precondition: this node is NOT full, children[i] IS full.
    void splitChild(int i) {
        TreeNode* full = children[i];
        int medianIndex = (degree - 1) / 2;
        int medianKey = full->keys[medianIndex];

        TreeNode* rightSibling = new TreeNode(degree);
        rightSibling->isLeaf = full->isLeaf;

        for (int j = medianIndex + 1; j < int(full->keys.size()); j++) {
            rightSibling->keys.push_back(full->keys[j]);
        }
        if (!full->isLeaf) {
            for (int j = medianIndex + 1; j < int(full->children.size()); j++) {
                rightSibling->children.push_back(full->children[j]);
            }
        }

        full->keys.resize(medianIndex);
        if (!full->isLeaf) {
            full->children.resize(medianIndex + 1);
        }

        keys.insert(keys.begin() + i, medianKey);
        children.insert(children.begin() + i + 1, rightSibling);
    }
};

namespace Btree {
    class Btree {
        public:
            virtual ~Btree() = default;
            virtual void insert(int key) = 0;
            virtual void build(std::vector<int>& keys) = 0;
            virtual void printTree() = 0;
    };

    class BtreeImpl : public Btree {
        private:
            int degree;
            TreeNode *root = nullptr;
        public:
            BtreeImpl( int degree) {
                if (degree <= 2) {
                    throw std::invalid_argument("Btree degree must be at least 3");

                }
                this->degree = degree;
                this->root = new TreeNode(degree); 
                std::cout << "BtreeImpl constructor called!" << std::endl;
            }

            void insert(int key) override {
                if (!root) {
                    root = new TreeNode(this->degree);
                }

                // The only place tree height grows: split a full root under a fresh parent.
                if (root->isFull()) {
                    TreeNode* newRoot = new TreeNode(this->degree);
                    newRoot->isLeaf = false;
                    newRoot->children.push_back(root);
                    newRoot->splitChild(0);
                    root = newRoot;
                }

                root->insertNonFull(key);
                std::cout << "Inserting " << key << " in btree" << std::endl;
            }

            void build(std::vector<int>& keys) override {
                std::cout << "Building btree from keys" << std::endl;
                for (auto it : keys) {
                    std::cout << "Key: " << it << std::endl;
                }
            }

            void printTree() override{
                if (!root) {
                    std::cout << "(empty tree)" << std::endl;
                    return;
                }
                printNode(root, 0);
            }

        private:
            void printNode(TreeNode* node, int depth) {
                if (!node) return;

                std::cout << std::string(depth * 4, ' ') << "[";
                for (size_t i = 0; i < node->keys.size(); ++i) {
                    if (i > 0) std::cout << " | ";
                    std::cout << node->keys[i];
                }
                std::cout << "]" << (node->isLeaf ? " (leaf)" : "") << std::endl;

                for (auto* child : node->children) {
                    printNode(child, depth + 1);
                }
            }

        public:

            ~BtreeImpl() override {
                std::cout << "BtreeImpl destructor called!" << std::endl;
            }
    };
}
