/*
 * Copyright (c) 2026, Kumosya
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * src/kernel/task/sched/rbtree.cc
 * Intrusive red-black tree ordered by virtual runtime and thread identity.
 */

#include "kernel/task.h"

namespace kernel::task {

// Promote the right child without changing in-order traversal. Its left
// subtree lies between node and right, so it becomes node's right subtree. The
// caller holds this queue lock and supplies a non-null right child.
void RunQueue::rotateLeft(Thread* node) {
    RunQueue* queue = this;
    Thread* right   = node->getRbRight();
    node->setRbRight(right->getRbLeft());
    if (right->getRbLeft() != nullptr) {
        right->getRbLeft()->setRbParent(node);
    }
    // Reconnect the promoted subtree to the old parent (or queue root) before
    // making node its left child. Both upward and downward links must agree.
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

// Mirror rotateLeft: promote the left child and move its middle subtree under
// node. Rotations preserve the ordering key; recoloring is the responsibility
// of the insertion or deletion repair that requested them.
void RunQueue::rotateRight(Thread* node) {
    RunQueue* queue = this;
    Thread* left    = node->getRbLeft();
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
    node->setRbParent(left);
}

// A red leaf adds no black height, but a red parent creates a red-red edge.
// Repair that edge locally or move it toward the root. A red parent cannot be
// the black root, so grandparent exists whenever the loop executes. All links
// and colors are protected by the caller-held queue lock.
void RunQueue::insertFixup(Thread* node) {
    RunQueue* queue = this;
    while (node->getRbParent() != nullptr && node->getRbParent()->getRbRed()) {
        Thread* parent      = node->getRbParent();
        Thread* grandparent = parent->getRbParent();
        if (parent == grandparent->getRbLeft()) {
            Thread* uncle = grandparent->getRbRight();
            // A red uncle lets both children become black while the
            // grandparent becomes red. Both sides gain the same black node;
            // only the edge above grandparent may now violate the rule, so
            // continue from grandparent.
            if (RunQueue::isRed(uncle)) {
                parent->setRbRed(false);
                uncle->setRbRed(false);
                grandparent->setRbRed(true);
                node = grandparent;
            } else {
                // An inner (left-right) edge cannot be repaired by one outer
                // rotation. First turn it into a left-left chain, then
                // recompute the relatives because the former parent is now
                // below the promoted node.
                if (node == parent->getRbRight()) {
                    node = parent;
                    queue->rotateLeft(node);
                    parent      = node->getRbParent();
                    grandparent = parent->getRbParent();
                }
                parent->setRbRed(false);
                grandparent->setRbRed(true);
                // With a black uncle and an outer chain, promote the blackened
                // parent and make grandparent red. This removes the red-red
                // edge without changing the black height seen by the rest of
                // the tree.
                queue->rotateRight(grandparent);
            }
        } else {
            // The parent is a right child: apply the same recoloring and
            // inner-to-outer conversion with left and right exchanged. The
            // invariants are identical.
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
    // Recoloring can carry a red node all the way to the root. A black root
    // finishes the repair; adding black height there affects every path
    // equally.
    queue->getRoot()->setRbRed(false);
}

// Insert a detached thread by (vruntime, id), repair colors, then publish
// queue membership and the cached minimum. The caller holds the queue lock;
// thread must not already be linked into any run queue.
void RunQueue::insertNode(Thread* thread) {
    RunQueue* queue = this;
    Thread* parent  = nullptr;
    Thread** link   = &queue->accessRoot();
    while (*link != nullptr) {
        parent = *link;
        link   = RunQueue::comesBefore(thread, parent) ? &parent->accessRbLeft()
                                                       : &parent->accessRbRight();
    }
    thread->setRbParent(parent);
    thread->setRbLeft(nullptr);
    thread->setRbRight(nullptr);
    // Using red here preserves all existing root-to-leaf black counts.
    // Starting black would require repairing black-height differences on every
    // insertion.
    thread->setRbRed(true);
    *link = thread;
    queue->insertFixup(thread);
    thread->setInRunQueue(true);
    queue->setRunnableCount(queue->getRunnableCount() + 1);
    // The scheduler selects through leftmost, not by walking the tree. Refresh
    // that cache after structural repair; runnableCount counts queued nodes
    // and excludes the running thread, which the scheduler has already
    // removed.
    queue->setLeftmost(RunQueue::minimum(queue->getRoot()));
    queue->setMinVruntime(queue->getLeftmost() == nullptr
                              ? queue->getMinVruntime()
                              : queue->getLeftmost()->getVruntime());
}

// Replace only target's connection to its parent, accepting a null leaf. The
// caller separately reconnects child subtrees and preserves colors. Deletion
// saves the replacement parent explicitly when replacement is null.
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

// Removing a black node leaves one fewer black node along the replacement
// branch. Treat node as carrying that deficit and either discharge it by
// rotation/recoloring or move it to parent. Null leaves count as black; parent
// is passed separately because a null node has no upward link. The caller
// holds the queue lock throughout removal and repair.
void RunQueue::deleteFixup(Thread* node, Thread* parent) {
    RunQueue* queue = this;
    while (node != queue->getRoot() && !RunQueue::isRed(node)) {
        if (parent == nullptr) {
            break;
        }
        if (node == parent->getRbLeft()) {
            Thread* sibling = parent->getRbRight();
            // A red sibling implies a black parent. Rotate and swap their
            // colors to obtain a black sibling; the deficit stays on the same
            // branch, but the remaining cases can now reason about black
            // sibling children.
            if (RunQueue::isRed(sibling)) {
                sibling->setRbRed(false);
                parent->setRbRed(true);
                queue->rotateLeft(parent);
                sibling = parent->getRbRight();
            }
            // There is no sibling subtree to rotate. Carry the deficit to the
            // parent and re-evaluate it against the next level.
            if (sibling == nullptr) {
                node   = parent;
                parent = node->getRbParent();
                continue;
            }
            // A black sibling with two black children can give up one black
            // level by turning red. The local paths balance, but parent now
            // inherits the deficit; a red parent will absorb it when blackened
            // after the loop.
            if (!RunQueue::isRed(sibling->getRbLeft()) &&
                !RunQueue::isRed(sibling->getRbRight())) {
                sibling->setRbRed(true);
                node   = parent;
                parent = node->getRbParent();
            } else {
                // For a left-side deficit, the far child is sibling's right
                // child. If only the near child is red, rotate sibling first
                // to create a red far child; that prepares the final rotation
                // without changing external black height.
                if (!RunQueue::isRed(sibling->getRbRight())) {
                    if (sibling->getRbLeft() != nullptr) {
                        sibling->getRbLeft()->setRbRed(false);
                    }
                    sibling->setRbRed(true);
                    queue->rotateRight(sibling);
                    sibling = parent->getRbRight();
                }
                // Transfer parent's old color to sibling, blacken parent and
                // the far child, then rotate toward the deficit. Both branches
                // regain equal black height; setting node to root ends repair
                // instead of propagating the deficit.
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
            // Mirror the left-deficit cases: sibling is on the left, the far
            // child is its left child, and rotation directions are reversed.
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
    // A red replacement absorbs the missing black node by becoming black. This
    // also enforces a black root when repair propagated to the top.
    if (node != nullptr) {
        node->setRbRed(false);
    }
}

// Detach thread while preserving every other Thread object's identity. A
// two-child deletion moves the successor node itself, not its payload: copying
// scheduling fields would change which thread the links represent. The caller
// holds this queue lock; a non-member is ignored.
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
        // The successor has no left child, so its old position can be replaced
        // by its right child. Remember the color at that old position: that is
        // where black height may be lost, regardless of thread's original
        // color.
        moved       = RunQueue::minimum(thread->getRbRight());
        movedWasRed = moved->getRbRed();
        child       = moved->getRbRight();
        // An immediate successor keeps its right child beneath itself; after
        // the move, a null replacement must therefore use moved as its repair
        // parent.
        if (moved->getRbParent() == thread) {
            childParent = moved;
            if (child != nullptr) {
                child->setRbParent(moved);
            }
        } else {
            // A deeper successor is first removed from its original branch.
            // Save that branch's parent before relocating moved, so repair
            // starts at the vacancy.
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
    // Removing red changes no black count. Removing black requires repair even
    // when child is null; childParent identifies the deficient branch.
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

// Null pointers represent black leaves, allowing repair cases to inspect
// absent children without inventing separately allocated sentinel nodes.
bool RunQueue::isRed(const Thread* node) {
    return node != nullptr && node->getRbRed();
}

// Thread IDs break equal-vruntime ties, giving a deterministic strict order
// without treating two runnable threads as the same tree entry.
bool RunQueue::comesBefore(const Thread* left, const Thread* right) {
    return left->getVruntime() < right->getVruntime() ||
           (left->getVruntime() == right->getVruntime() &&
            left->getId() < right->getId());
}

// Follow only left links to find the earliest key; an empty subtree has no
// minimum. This helper assumes the caller prevents concurrent tree mutation.
Thread* RunQueue::minimum(Thread* node) {
    while (node != nullptr && node->getRbLeft() != nullptr) {
        node = node->getRbLeft();
    }
    return node;
}

// Initialize freshly allocated queue storage before publishing it to a CPU.
// This is not a reset operation for a queue containing live threads.
void RunQueue::clear() {
    RunQueue* queue = this;
    uint8_t* bytes  = reinterpret_cast<uint8_t*>(queue);
    for (uint64_t index = 0; index < sizeof(RunQueue); ++index) {
        bytes[index] = 0;
    }
}

}  // namespace kernel::task
