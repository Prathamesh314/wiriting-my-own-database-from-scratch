#pragma once
#include <iostream>
#include <vector>

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
        public:
            BtreeImpl( int degree) {
                this->degree = degree;
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
