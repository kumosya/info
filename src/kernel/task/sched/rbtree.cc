/*
 * Copyright (c) 2026, Kumosya
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * src/kernel/task/sched/rbtree.cc
 * 用红黑树维护可运行线程顺序，最左节点就是下一候选线程。
 */

#include "kernel/task.h"

namespace task {

    /*
     * 对节点做左旋，保持线程排序不变，同时调整父子关系和根指针。
     * 调用者已经持有运行队列锁。
     */
    void RunQueue::rotateLeft(Thread* node) {
        RunQueue* queue = this;
        Thread* right   = node->getRbRight();
        node->setRbRight(right->getRbLeft());

        if (right->getRbLeft() != nullptr) {
            right->getRbLeft()->setRbParent(node);
        }

        // 先把提升的子树接回旧父节点或根，再让原节点成为其左孩子，上下链接必须一致。
        right->setRbParent(node->getRbParent());

        if (node->getRbParent() == nullptr) {
            queue->setRoot(right);
        } else if (node == node->getRbParent()->getRbLeft()) {
            node->getRbParent()->setRbLeft(right);
        } else {
            node->getRbParent()->setRbRight(right);
        }

        right->setRbLeft(node);
        node->setRbParent(right);
    }

    /*
     * 对节点做右旋，与左旋对称，用于恢复红黑树的平衡条件。
     */
    void RunQueue::rotateRight(Thread* node) {
        RunQueue* queue = this;

        // 先保存将被提升的左孩子，旋转只改父子链接，不改变线程虚拟时间的比较顺序。
        Thread* left = node->getRbLeft();
        node->setRbLeft(left->getRbRight());

        if (left->getRbRight() != nullptr) {
            left->getRbRight()->setRbParent(node);
        }

        left->setRbParent(node->getRbParent());

        if (node->getRbParent() == nullptr) {
            queue->setRoot(left);
        } else if (node == node->getRbParent()->getRbRight()) {
            node->getRbParent()->setRbRight(left);
        } else {
            node->getRbParent()->setRbLeft(left);
        }

        left->setRbRight(node);

        // 原节点接到提升节点的右侧，父指针也要反向更新，保持从根向下和从节点向上看到同一棵树。
        node->setRbParent(left);
    }

    /*
     * 修复插入后的颜色冲突。
     * 根据父节点和叔节点颜色选择改色或旋转，使查找深度维持在对数级别。
     */
    void RunQueue::insertFixup(Thread* node) {
        RunQueue* queue = this;

        while (node->getRbParent() != nullptr &&
               node->getRbParent()->getRbRed()) {
            Thread* parent      = node->getRbParent();
            Thread* grandparent = parent->getRbParent();

            if (parent == grandparent->getRbLeft()) {
                Thread* uncle = grandparent->getRbRight();
                // 叔节点为红色时，父与叔改黑、祖父改红；两条分支黑节点数同时增加，只需继续检查祖父上方是否冲突。
                if (RunQueue::isRed(uncle)) {
                    parent->setRbRed(false);
                    uncle->setRbRed(false);
                    grandparent->setRbRed(true);
                    node = grandparent;
                } else {
                    // 左右折线不能一次外旋修好；先把内侧折线转成同侧直线，再重新计算父子关系。
                    if (node == parent->getRbRight()) {
                        node = parent;
                        queue->rotateLeft(node);
                        parent      = node->getRbParent();
                        grandparent = parent->getRbParent();
                    }

                    parent->setRbRed(false);
                    grandparent->setRbRed(true);
                    // 叔节点为黑且节点在外侧时，提升并染黑父节点、染红祖父，旋转后消除连续红节点而不改变外围黑节点数。
                    queue->rotateRight(grandparent);
                }
            } else {
                // 父节点在右侧时对称处理，交换左右方向，维持相同平衡规则。
                Thread* uncle = grandparent->getRbLeft();

                if (RunQueue::isRed(uncle)) {
                    parent->setRbRed(false);
                    uncle->setRbRed(false);
                    grandparent->setRbRed(true);
                    node = grandparent;
                } else {
                    if (node == parent->getRbLeft()) {
                        node = parent;
                        queue->rotateRight(node);
                        parent      = node->getRbParent();
                        grandparent = parent->getRbParent();
                    }

                    parent->setRbRed(false);
                    grandparent->setRbRed(true);
                    queue->rotateLeft(grandparent);
                }
            }
        }

        // 冲突可能一直传到根；最终将根染黑，对所有路径同时增加一层黑色。
        queue->getRoot()->setRbRed(false);
    }

    /*
     * 按虚拟运行时间把线程插入树中，更新最左节点后修复平衡。
     * 相同运行时间再按线程编号确定稳定顺序。
     */
    void RunQueue::insertNode(Thread* thread) {
        RunQueue* queue = this;
        Thread* parent  = nullptr;
        Thread** link   = &queue->accessRoot();

        while (*link != nullptr) {
            parent = *link;
            link   = RunQueue::comesBefore(thread, parent)
                         ? &parent->accessRbLeft()
                         : &parent->accessRbRight();
        }

        thread->setRbParent(parent);
        thread->setRbLeft(nullptr);
        thread->setRbRight(nullptr);
        // 新节点先染红可保持原有路径的黑节点数；若直接染黑，每次插入都要处理黑色层数差异。
        thread->setRbRed(true);
        *link = thread;
        queue->insertFixup(thread);
        thread->setInRunQueue(true);
        queue->setRunnableCount(queue->getRunnableCount() + 1);
        // 调度器通过最左节点直接选择线程，结构修复后必须更新这个缓存；运行中的线程已出队，不计入排队数量。
        queue->setLeftmost(RunQueue::minimum(queue->getRoot()));
        queue->setMinVruntime(queue->getLeftmost() == nullptr
                                  ? queue->getMinVruntime()
                                  : queue->getLeftmost()->getVruntime());
    }

    /*
     * 用一个子树替换另一个子树，统一处理根节点和普通父节点的连接；不在这里完成颜色修复。
     */
    void RunQueue::transplant(Thread* target, Thread* replacement) {
        RunQueue* queue = this;

        if (target->getRbParent() == nullptr) {
            queue->setRoot(replacement);
        } else if (target == target->getRbParent()->getRbLeft()) {
            target->getRbParent()->setRbLeft(replacement);
        } else {
            target->getRbParent()->setRbRight(replacement);
        }

        if (replacement != nullptr) {
            replacement->setRbParent(target->getRbParent());
        }
    }

    /*
     * 修复删除黑节点后各路径黑节点数量不一致的问题。
     * 根据兄弟节点及其子节点颜色，逐层改色或旋转。
     */
    void RunQueue::deleteFixup(Thread* node, Thread* parent) {
        RunQueue* queue = this;

        while (node != queue->getRoot() && !RunQueue::isRed(node)) {
            if (parent == nullptr) {
                break;
            }

            if (node == parent->getRbLeft()) {
                Thread* sibling = parent->getRbRight();
                // 红色兄弟意味着父节点为黑，先旋转并交换颜色，变成黑兄弟情形再处理缺失的黑色层数。
                if (RunQueue::isRed(sibling)) {
                    sibling->setRbRed(false);
                    parent->setRbRed(true);
                    queue->rotateLeft(parent);
                    sibling = parent->getRbRight();
                }

                // 没有兄弟子树可旋转时，把缺少的黑色层数向父节点传递，再检查上一层。
                if (sibling == nullptr) {
                    node   = parent;
                    parent = node->getRbParent();
                    continue;
                }

                // 黑兄弟的两个孩子都黑时，将兄弟改红来平衡局部路径，再把少一层黑色的问题交给父节点。
                if (!RunQueue::isRed(sibling->getRbLeft()) &&
                    !RunQueue::isRed(sibling->getRbRight())) {
                    sibling->setRbRed(true);
                    node   = parent;
                    parent = node->getRbParent();
                } else {
                    // 当前节点在左侧时，兄弟的右孩子为远侧；若只有近侧孩子红，先旋转兄弟，转成远侧为红的情形。
                    if (!RunQueue::isRed(sibling->getRbRight())) {
                        if (sibling->getRbLeft() != nullptr) {
                            sibling->getRbLeft()->setRbRed(false);
                        }

                        sibling->setRbRed(true);
                        queue->rotateRight(sibling);
                        sibling = parent->getRbRight();
                    }

                    // 兄弟继承父颜色，父和远侧孩子改黑，再向当前分支旋转；两侧黑色层数恢复一致，修复即可结束。
                    sibling->setRbRed(parent->getRbRed());
                    parent->setRbRed(false);

                    if (sibling->getRbRight() != nullptr) {
                        sibling->getRbRight()->setRbRed(false);
                    }

                    queue->rotateLeft(parent);
                    node   = queue->getRoot();
                    parent = nullptr;
                }
            } else {
                // 当前节点在右侧时镜像处理：兄弟在左，远侧为左孩子，旋转方向全部相反。
                Thread* sibling = parent->getRbLeft();

                if (RunQueue::isRed(sibling)) {
                    sibling->setRbRed(false);
                    parent->setRbRed(true);
                    queue->rotateRight(parent);
                    sibling = parent->getRbLeft();
                }

                if (sibling == nullptr) {
                    node   = parent;
                    parent = node->getRbParent();
                    continue;
                }

                if (!RunQueue::isRed(sibling->getRbRight()) &&
                    !RunQueue::isRed(sibling->getRbLeft())) {
                    sibling->setRbRed(true);
                    node   = parent;
                    parent = node->getRbParent();
                } else {
                    if (!RunQueue::isRed(sibling->getRbLeft())) {
                        if (sibling->getRbRight() != nullptr) {
                            sibling->getRbRight()->setRbRed(false);
                        }

                        sibling->setRbRed(true);
                        queue->rotateLeft(sibling);
                        sibling = parent->getRbLeft();
                    }

                    sibling->setRbRed(parent->getRbRed());
                    parent->setRbRed(false);

                    if (sibling->getRbLeft() != nullptr) {
                        sibling->getRbLeft()->setRbRed(false);
                    }

                    queue->rotateRight(parent);
                    node   = queue->getRoot();
                    parent = nullptr;
                }
            }
        }

        // 红色替代节点改黑即可补上缺少的一层，问题传到根时也保证根为黑色。
        if (node != nullptr) {
            node->setRbRed(false);
        }
    }

    /*
     * 删除指定线程节点。
     * 先完成结构替换，再按原颜色决定是否修复，并更新最早应运行的最左节点。
     */
    void RunQueue::removeNode(Thread* thread) {
        RunQueue* queue = this;

        if (!thread->getInRunQueue()) {
            return;
        }

        Thread* moved    = thread;
        bool movedWasRed = moved->getRbRed();
        Thread* child;
        Thread* childParent;

        if (thread->getRbLeft() == nullptr) {
            child       = thread->getRbRight();
            childParent = thread->getRbParent();
            queue->transplant(thread, thread->getRbRight());
        } else if (thread->getRbRight() == nullptr) {
            child       = thread->getRbLeft();
            childParent = thread->getRbParent();
            queue->transplant(thread, thread->getRbLeft());
        } else {
            // 后继没有左孩子，其原位置由右孩子替代；记录的是后继原位置颜色，因为那里可能少一层黑色。
            moved       = RunQueue::minimum(thread->getRbRight());
            movedWasRed = moved->getRbRed();
            child       = moved->getRbRight();
            // 后继正好是右孩子时，其右孩子仍留在它下面；空替代节点也必须以这个后继作为修复父节点。
            if (moved->getRbParent() == thread) {
                childParent = moved;

                if (child != nullptr) {
                    child->setRbParent(moved);
                }
            } else {
                // 后继位于更深位置时，先从原分支摘下；移动前保存旧父节点，使修复从真正空出的位置开始。
                childParent = moved->getRbParent();
                queue->transplant(moved, moved->getRbRight());
                moved->setRbRight(thread->getRbRight());
                moved->getRbRight()->setRbParent(moved);
            }

            queue->transplant(thread, moved);
            moved->setRbLeft(thread->getRbLeft());
            moved->getRbLeft()->setRbParent(moved);
            moved->setRbRed(thread->getRbRed());
        }

        // 删除红节点不改变黑色层数，删除黑节点则即使孩子为空也要修复，由
        // childParent 标明缺少节点的分支。
        if (!movedWasRed) {
            queue->deleteFixup(child, childParent);
        }

        thread->setRbParent(nullptr);
        thread->setRbLeft(nullptr);
        thread->setRbRight(nullptr);
        thread->setInRunQueue(false);
        queue->setRunnableCount(queue->getRunnableCount() - 1);
        queue->setLeftmost(RunQueue::minimum(queue->getRoot()));

        if (queue->getLeftmost() != nullptr) {
            queue->setMinVruntime(queue->getLeftmost()->getVruntime());
        }
    }

    /*
     * 判断节点是否为红色；空节点按黑色处理，简化边界判断。
     */
    bool RunQueue::isRed(const Thread* node) {
        return node != nullptr && node->getRbRed();
    }

    /*
     * 比较两条线程的队列顺序，先比较虚拟运行时间，再用线程编号打破相同值。
     */
    bool RunQueue::comesBefore(const Thread* left, const Thread* right) {
        return left->getVruntime() < right->getVruntime() ||
               (left->getVruntime() == right->getVruntime() &&
                left->getId() < right->getId());
    }

    /*
     * 沿左子树找到最小节点，即该子树中最早应运行的线程。
     */
    Thread* RunQueue::minimum(Thread* node) {
        while (node != nullptr && node->getRbLeft() != nullptr) {
            node = node->getRbLeft();
        }

        return node;
    }

    /*
     * 清空运行队列的树、计数和调度时间状态，供初始化使用。
     */
    void RunQueue::clear() {
        RunQueue* queue = this;
        uint8_t* bytes  = reinterpret_cast<uint8_t*>(queue);

        for (uint64_t index = 0; index < sizeof(RunQueue); ++index) {
            bytes[index] = 0;
        }
    }

}  // namespace task
