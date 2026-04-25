#pragma once

#include <algorithm>
#include <cstddef>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "Logger.h"

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

    // Minimum keys a non-root node should hold. Mirrors splitChild's medianIndex:
    // a freshly-split half always has exactly this many keys.
    int minKeys() const {
        return (degree - 1) / 2;
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

    TreeNode* findKey(int key) {
        int i = 0;
        while (i < int(keys.size()) and key > keys[i]){
            i++;
        }

        if (i < int(keys.size()) and key == keys[i]){
            return this;
        }

        if(this->isLeaf){
            return nullptr;
        }

        return this->children[i]->findKey(key);
    }

    void deleteKey(int key) {
        int i = std::lower_bound(keys.begin(), keys.end(), key) - keys.begin();

        if (i < int(keys.size()) && keys[i] == key) {
            // Cases 1 & 2: key lives in this node
            if (isLeaf) {
                keys.erase(keys.begin() + i);                 // Case 1
            } else {
                deleteFromInternal(i, key);                   // Case 2
            }
            return;
        }

        // Case 3: key is below children[i] (or not in tree)
        if (isLeaf) {
            Logger::warn("Key ", key, " not found in tree");
            return;
        }

        // Preemptive: ensure children[i] has > minKeys() so it can safely lose one.
        bool mergedWithLeft = false;
        if (int(children[i]->keys.size()) <= minKeys()) {
            mergedWithLeft = fixUnderflow(i);
        }

        // If a left-merge happened, our target is now at index i-1 (two children collapsed into one).
        int idx = mergedWithLeft ? i - 1 : i;
        children[idx]->deleteKey(key);
    }

    // Case 2: key sits in this internal node at index i. Three sub-cases.
    void deleteFromInternal(int i, int key) {
        if (int(children[i]->keys.size()) > minKeys()) {
            // (a) Steal predecessor from left subtree, then delete it from there.
            int predKey = predecessor(i);
            keys[i] = predKey;
            children[i]->deleteKey(predKey);
        } else if (int(children[i + 1]->keys.size()) > minKeys()) {
            // (b) Steal successor from right subtree, then delete it from there.
            int succKey = successor(i);
            keys[i] = succKey;
            children[i + 1]->deleteKey(succKey);
        } else {
            // (c) Both adjacent children at minimum: merge them with keys[i] in between, recurse.
            merge(i);
            children[i]->deleteKey(key);
        }
    }

    // Largest key in left subtree.
    int predecessor(int i) {
        TreeNode* cur = children[i];
        while (!cur->isLeaf) {
            cur = cur->children.back();
        }
        return cur->keys.back();
    }

    // Smallest key in right subtree.
    int successor(int i) {
        TreeNode* cur = children[i + 1];
        while (!cur->isLeaf) {
            cur = cur->children.front();
        }
        return cur->keys.front();
    }

    // Ensures children[i] is safe to descend into for delete (more than minKeys keys).
    // Returns true iff a left-merge happened (caller must shift its index by -1).
    bool fixUnderflow(int i) {
        if (i > 0 && int(children[i - 1]->keys.size()) > minKeys()) {
            borrowFromLeft(i);
            return false;
        }
        if (i < int(children.size()) - 1 && int(children[i + 1]->keys.size()) > minKeys()) {
            borrowFromRight(i);
            return false;
        }
        // Both eligible siblings (or the only sibling) are at minimum: merge.
        if (i > 0) {
            merge(i - 1);
            return true;
        }
        merge(i);
        return false;
    }

    // Rotate via parent: parent's separator drops into children[i], leftSib's last key rises.
    void borrowFromLeft(int i) {
        TreeNode* child = children[i];
        TreeNode* leftSib = children[i - 1];

        child->keys.insert(child->keys.begin(), keys[i - 1]);
        if (!child->isLeaf) {
            child->children.insert(child->children.begin(), leftSib->children.back());
            leftSib->children.pop_back();
        }
        keys[i - 1] = leftSib->keys.back();
        leftSib->keys.pop_back();
    }

    // Symmetric: parent's separator drops into children[i], rightSib's first key rises.
    void borrowFromRight(int i) {
        TreeNode* child = children[i];
        TreeNode* rightSib = children[i + 1];

        child->keys.push_back(keys[i]);
        if (!child->isLeaf) {
            child->children.push_back(rightSib->children.front());
            rightSib->children.erase(rightSib->children.begin());
        }
        keys[i] = rightSib->keys.front();
        rightSib->keys.erase(rightSib->keys.begin());
    }

    // Merge children[i] + keys[i] + children[i+1] into children[i]. Removes the separator and right pointer.
    void merge(int i) {
        TreeNode* left = children[i];
        TreeNode* right = children[i + 1];
        Logger::debug("merge: pulling separator ", keys[i], " into child idx ", i);

        left->keys.push_back(keys[i]);
        for (int k : right->keys) left->keys.push_back(k);
        if (!left->isLeaf) {
            for (TreeNode* c : right->children) left->children.push_back(c);
        }

        keys.erase(keys.begin() + i);
        children.erase(children.begin() + i + 1);

        delete right;
    }

    // Precondition: this node is NOT full, children[i] IS full.
    void splitChild(int i) {
        TreeNode* full = children[i];
        int medianIndex = (degree - 1) / 2;
        int medianKey = full->keys[medianIndex];
        Logger::debug("splitChild: promoting median ", medianKey, " (child idx ", i, ")");

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
            virtual TreeNode* findKey(int key) = 0;
            virtual void deleteKey(int key) = 0;
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
                Logger::debug("BtreeImpl constructor called (degree=", degree, ")");
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
                Logger::info("Inserted ", key, " into btree");
            }
            
            TreeNode* findKey(int key) override {
                if (!root) {
                    Logger::warn("Tree is empty; cannot find key: ", key);
                    return nullptr;
                }

                return root->findKey(key);
            }

            void deleteKey(int key) override {
                if (!root || root->keys.empty()) {
                    Logger::warn("Tree is empty; cannot delete key: ", key);
                    return;
                }

                root->deleteKey(key);

                // Tree height shrinks when root's only key gets pulled down by a merge.
                // The merged child becomes the new root.
                if (root->keys.empty() && !root->isLeaf) {
                    TreeNode* oldRoot = root;
                    root = root->children[0];
                    delete oldRoot;
                }

                Logger::info("Deleted ", key, " from btree (if present)");
            }

            void build(std::vector<int>& keys) override {
                Logger::info("Building btree from ", keys.size(), " keys");
                for (auto it : keys) {
                    Logger::debug("  key: ", it);
                }
            }

            void printTree() override {
                if (!root) {
                    Logger::print("(empty tree)");
                    return;
                }
                printNode(root, 0);
            }

        private:
            void printNode(TreeNode* node, int depth) {
                if (!node) return;

                std::ostringstream line;
                line << std::string(depth * 4, ' ') << "[";
                for (size_t i = 0; i < node->keys.size(); ++i) {
                    if (i > 0) line << " | ";
                    line << node->keys[i];
                }
                line << "]" << (node->isLeaf ? " (leaf)" : "");
                Logger::print(line.str());

                for (auto* child : node->children) {
                    printNode(child, depth + 1);
                }
            }

        public:

            ~BtreeImpl() override {
                Logger::debug("BtreeImpl destructor called");
            }
    };
}
