/**
 * @file task/rbtree.cc
 * @brief Red-Black Tree implementation for CFS
 * @author Kumosya, 2025-2026
 **/

#include <cstdint>

#include "kernel/cpu.h"
#include "kernel/task.h"
#include "kernel/tty.h"

namespace task::cfs {

static inline void RbSetRed(Entity *node) {
    if (node) {
        node->rb_is_red = true;
    }
}

static inline void RbSetBlack(Entity *node) {
    if (node) {
        node->rb_is_red = false;
    }
}

static inline bool RbIsRed(Entity *node) {
    return node && node->rb_is_red;
}

static inline void RbSetParent(Entity *node, Entity *parent) {
    if (node) {
        node->rb_parent = parent;
    }
}

static void rb_print_node(Entity *node, int depth, Entity **visited,
                          int &visited_count, int max_visited) {
    if (!node) return;

    // 检测指针环：检查当前节点是否已经访问过
    for (int i = 0; i < visited_count; i++) {
        if (visited[i] == node) {
            tty::printk(" [LOOP DETECTED at node %d!]", node->pcb->GetPid());
            return;
        }
    }

    // 记录已访问节点
    if (visited_count < max_visited) {
        visited[visited_count++] = node;
    }

    tty::printk("%d[vr=%d,color=%c]", node->pcb->GetPid(), node->vruntime,
                RbIsRed(node) ? 'R' : 'B');

    if (node->rb_left || node->rb_right) {
        tty::printk("->(");

        if (node->rb_left) {
            rb_print_node(node->rb_left, depth + 1, visited, visited_count,
                          max_visited);
            if (node->rb_right) {
                tty::printk(",");
                rb_print_node(node->rb_right, depth + 1, visited,
                              visited_count, max_visited);
            }
        } else if (node->rb_right) {
            tty::printk("nullptr,");
            rb_print_node(node->rb_right, depth + 1, visited, visited_count,
                          max_visited);
        }
        tty::printk(")");
    }
}

void Rq::RbPrintTree() {
    tty::printk("\n=== RBTree ===\n");
    tty::printk("root=%d, leftmost=%d\n",
                curr_sched.rb_root ? curr_sched.rb_root->pcb->GetPid() : -1,
                curr_sched.leftmost ? curr_sched.leftmost->pcb->GetPid() : -1);
    tty::printk("nr_running=%d, min_vruntime=%d\n", curr_sched.nr_running,
                curr_sched.min_vruntime);

    if (curr_sched.rb_root) {
        tty::printk("Tree: ");
        // 防止指针环导致的无限递归，分配访问记录数组
        Entity *visited[256];
        int visited_count = 0;
        rb_print_node(curr_sched.rb_root, 0, visited, visited_count, 256);
        tty::printk("\n");
    }
    tty::printk("=============\n");
}

void Rq::RbLeftRotate(Entity *x) {
    if (!x || !x->rb_right || x->rb_right == x) {  // 禁止自旋转
        // tty::printk("[LeftRotate] invalid: x=%d, right=%d (self rotate)\n",
        //             x ? x->GetPid() : -1, x->rb_right ? x->rb_right->GetPid() :
        //             -1);
        return;
    }
    Entity *y = x->rb_right;
    if (!y) return;
    // tty::printk("[LeftRotate] x=%d, y=%d\n", x->GetPid(), y->GetPid());

    x->rb_right = y->rb_left;

    if (y->rb_left != nullptr) {
        y->rb_left->rb_parent = x;
    }

    y->rb_parent = x->rb_parent;

    if (x->rb_parent == nullptr) {
        curr_sched.rb_root = y;
    } else if (x == x->rb_parent->rb_left) {
        x->rb_parent->rb_left = y;
    } else {
        x->rb_parent->rb_right = y;
    }

    y->rb_left   = x;
    x->rb_parent = y;

    // tty::printk("[LeftRotate] done: x.parent=%d, y.parent=%d\n",
    //             x->rb_parent ? x->rb_parent->GetPid() : -1,
    //             y->rb_parent ? y->rb_parent->GetPid() : -1);
}

void Rq::RbRightRotate(Entity *y) {
    if (!y || !y->rb_left || y->rb_left == y) {  // 禁止自旋转
        // tty::printk("[RightRotate] invalid: y=%d, left=%d (self rotate)\n",
        //             y ? y->GetPid() : -1, y->rb_left ? y->rb_left->GetPid() :
        //             -1);
        return;
    }
    Entity *x = y->rb_left;
    if (!x) return;
    // tty::printk("[RightRotate] y=%d, x=%d\n", y->GetPid(), x->GetPid());

    y->rb_left = x->rb_right;

    if (x->rb_right != nullptr) {
        x->rb_right->rb_parent = y;
    }

    x->rb_parent = y->rb_parent;

    if (y->rb_parent == nullptr) {
        curr_sched.rb_root = x;
    } else if (y == y->rb_parent->rb_right) {
        y->rb_parent->rb_right = x;
    } else {
        y->rb_parent->rb_left = x;
    }

    x->rb_right  = y;
    y->rb_parent = x;

    // tty::printk("[RightRotate] done: x.parent=%d, y.parent=%d\n",
    //             x->rb_parent ? x->rb_parent->GetPid() : -1,
    //             y->rb_parent ? y->rb_parent->GetPid() : -1);
}

void Rq::RbInsertColorFixup(Entity *node) {
    // tty::printk("[InsertFixup] node=%d start\n", node ? node->GetPid() : -1);
    while (node && node->rb_parent && RbIsRed(node->rb_parent)) {
        Entity *parent      = node->rb_parent;
        Entity *grandparent = parent->rb_parent;
        if (!grandparent || parent == node || grandparent == parent ||
            grandparent == node) {
            // tty::printk("[InsertFixup] pointer loop detected, break\n");
            break;
        }
        if (parent == grandparent->rb_left) {
            Entity *uncle = grandparent->rb_right;
            // tty::printk("[InsertFixup] parent=left, uncle=%d, is_red=%d\n",
            //             uncle ? uncle->GetPid() : -1, uncle ? (int)RbIsRed(uncle)
            //             : -1);

            if (RbIsRed(uncle)) {
                // tty::printk("[InsertFixup] Case 1: uncle red, recolor\n");
                RbSetBlack(parent);
                RbSetBlack(uncle);
                RbSetRed(grandparent);
                node = grandparent;
            } else {
                if (node == parent->rb_right) {
                    // tty::printk("[InsertFixup] Case 2: node is right child,
                    // rotate\n");
                    node = parent;
                    RbLeftRotate(node);
                    parent = node->rb_parent;
                }

                // tty::printk("[InsertFixup] Case 3: recolor and rotate\n");
                RbSetBlack(parent);
                RbSetRed(grandparent);
                RbRightRotate(grandparent);
            }
        } else {
            Entity *uncle = grandparent->rb_left;
            // tty::printk("[InsertFixup] parent=right, uncle=%d, is_red=%d\n",
            //             uncle ? uncle->GetPid() : -1, uncle ? (int)RbIsRed(uncle)
            //             : -1);

            if (RbIsRed(uncle)) {
                // tty::printk("[InsertFixup] Case 1: uncle red, recolor\n");
                RbSetBlack(parent);
                RbSetBlack(uncle);
                RbSetRed(grandparent);
                node = grandparent;
            } else {
                if (node == parent->rb_left) {
                    // tty::printk("[InsertFixup] Case 2: node is left child,
                    // rotate\n");
                    node = parent;
                    RbRightRotate(node);
                    parent = node->rb_parent;
                }

                // tty::printk("[InsertFixup] Case 3: recolor and rotate\n");
                RbSetBlack(parent);
                RbSetRed(grandparent);
                RbLeftRotate(grandparent);
            }
        }
    }

    if (curr_sched.rb_root) {
        RbSetBlack(curr_sched.rb_root);
    }

    // tty::printk("[InsertFixup] done, root=%d\n", curr_sched.rb_root ?
    // curr_sched.rb_root->GetPid() : -1);
}

void Rq::RbEraseColorFixup(Entity *node, Entity *parent) {
    // tty::printk("[EraseFixup] node=%d, parent=%d start\n",
    //             node ? node->GetPid() : -1, parent ? parent->GetPid() : -1);

    while (node != curr_sched.rb_root && parent &&
           (node == nullptr || !RbIsRed(node))) {
        if (node == parent->rb_left) {
            Entity *sibling = parent->rb_right;
            if (!sibling) break;

            // tty::printk("[EraseFixup] node=left, sibling=%d, is_red=%d\n",
            //             sibling ? sibling->GetPid() : -1, sibling ?
            //             RbIsRed(sibling) : -1);

            if (RbIsRed(sibling)) {
                // tty::printk("[EraseFixup] sibling red, recolor and
                // rotate\n");
                RbSetBlack(sibling);
                RbSetRed(parent);
                RbLeftRotate(parent);
                sibling = parent->rb_right;
            }

            if ((sibling->rb_left == nullptr ||
                 !RbIsRed(sibling->rb_left)) &&
                (sibling->rb_right == nullptr ||
                 !RbIsRed(sibling->rb_right))) {
                // tty::printk("[EraseFixup] sibling children black, recolor
                // sibling\n");
                RbSetRed(sibling);
                node   = parent;
                parent = node->rb_parent;
            } else {
                if (sibling->rb_right == nullptr ||
                    !RbIsRed(sibling->rb_right)) {
                    // tty::printk("[EraseFixup] sibling right black, rotate
                    // sibling right\n");
                    if (sibling->rb_left) {
                        RbSetBlack(sibling->rb_left);
                    }
                    RbSetRed(sibling);
                    RbRightRotate(sibling);
                    sibling = parent->rb_right;
                }

                // tty::printk("[EraseFixup] recolor and rotate parent\n");
                RbSetBlack(sibling->rb_right);
                RbSetRed(parent);
                RbLeftRotate(parent);
                node = curr_sched.rb_root;
                break;
            }
        } else {
            Entity *sibling = parent->rb_left;
            if (!sibling) break;

            // tty::printk("[EraseFixup] node=right, sibling=%d, is_red=%d\n",
            //             sibling ? sibling->GetPid() : -1, sibling ?
            //             RbIsRed(sibling) : -1);

            if (RbIsRed(sibling)) {
                // tty::printk("[EraseFixup] sibling red, recolor and
                // rotate\n");
                RbSetBlack(sibling);
                RbSetRed(parent);
                RbRightRotate(parent);
                sibling = parent->rb_left;
            }

            if ((sibling->rb_right == nullptr ||
                 !RbIsRed(sibling->rb_right)) &&
                (sibling->rb_left == nullptr ||
                 !RbIsRed(sibling->rb_left))) {
                // tty::printk("[EraseFixup] sibling children black, recolor
                // sibling\n");
                RbSetRed(sibling);
                node   = parent;
                parent = node->rb_parent;
            } else {
                if (sibling->rb_left == nullptr ||
                    !RbIsRed(sibling->rb_left)) {
                    // tty::printk("[EraseFixup] sibling left black, rotate
                    // sibling left\n");
                    if (sibling->rb_right) {
                        RbSetBlack(sibling->rb_right);
                    }
                    RbSetRed(sibling);
                    RbLeftRotate(sibling);
                    sibling = parent->rb_left;
                }

                // tty::printk("[EraseFixup] recolor and rotate parent\n");
                RbSetBlack(sibling->rb_left);
                RbSetRed(parent);
                RbRightRotate(parent);
                node = curr_sched.rb_root;
                break;
            }
        }
    }

    if (node) {
        RbSetBlack(node);
    }

    // tty::printk("[EraseFixup] done\n");
}

void Rq::RbInitNode(Entity *node) {
    if (!node) return;
    node->rb_left   = nullptr;
    node->rb_right  = nullptr;
    node->rb_parent = nullptr;
    node->rb_is_red = true;
}

void Rq::RbInsert(Entity *node) {
    if (!node) return;

    //tty::printk("RbInsert: %d, vruntime: %d\n", node->pcb->GetPid(),
    //            node->vruntime);
    RbInitNode(node);

    Entity *y = nullptr;
    Entity *x = curr_sched.rb_root;

    while (x != nullptr) {
        y = x;
        if (node->vruntime < x->vruntime) {
            x = x->rb_left;
        } else {
            x = x->rb_right;
        }
    }

    node->rb_parent = y;

    if (y == nullptr) {
        curr_sched.rb_root = node;
        RbSetBlack(node);  // 根节点设为黑色
    } else if (node->vruntime < y->vruntime) {
        y->rb_left = node;
    } else {
        y->rb_right = node;
    }

    if (node->rb_parent == nullptr) {
        curr_sched.leftmost = node;
        // tty::printk("leftmost: %d\n", curr_sched.leftmost->GetPid());
    } else if (node->rb_parent->rb_parent != nullptr) {
        RbInsertColorFixup(node);
    }

    // 每次插入后重新计算全局leftmost
    Entity *leftmost = curr_sched.rb_root;
    if (leftmost) {
        while (leftmost->rb_left) {
            leftmost = leftmost->rb_left;
        }
        curr_sched.leftmost     = leftmost;
        curr_sched.min_vruntime = leftmost->vruntime;  // 更新min_vruntime
        // tty::printk("leftmost: %d\n", curr_sched.leftmost->GetPid());
    }
    // RbPrintTree();
}

void Rq::RbReplaceNode(Entity *u, Entity *v) {
    // tty::printk("[ReplaceNode] u=%d, v=%d\n", u ? u->GetPid() : -1, v ? v->GetPid() :
    // -1);

    if (u->rb_parent == nullptr) {
        curr_sched.rb_root = v;
    } else if (u == u->rb_parent->rb_left) {
        u->rb_parent->rb_left = v;
    } else {
        u->rb_parent->rb_right = v;
    }

    if (v != nullptr) {
        v->rb_parent = u->rb_parent;
    }
}

void Rq::RbErase(Entity *node) {
    if (node == nullptr) {
        // tty::printk("[RbErase] ERROR: node is nullptr!\n");
        return;
    }

    // tty::printk("[RbErase] start: node=%d, rb_root=%d, nr_running=%d\n",
    //             node->GetPid(),
    //             curr_sched.rb_root ? curr_sched.rb_root->GetPid() : -1,
    //             curr_sched.nr_running);

    Entity *y        = node;
    Entity *x        = nullptr;
    Entity *x_parent = nullptr;
    bool y_original_red = RbIsRed(y);

    // tty::printk("[RbErase] node=%d, left=%d, right=%d, parent=%d\n",
    //             node->GetPid(),
    //             node->rb_left ? node->rb_left->GetPid() : -1,
    //             node->rb_right ? node->rb_right->GetPid() : -1,
    //             node->rb_parent ? node->rb_parent->GetPid() : -1);

    if (node->rb_left == nullptr) {
        x        = node->rb_right;
        x_parent = node->rb_parent;
        // tty::printk("[RbErase] left is nullptr, x=%d\n", x ? x->GetPid() : -1);
        RbReplaceNode(node, x);
    } else if (node->rb_right == nullptr) {
        x        = node->rb_left;
        x_parent = node->rb_parent;
        // tty::printk("[RbErase] right is nullptr, x=%d\n", x ? x->GetPid() : -1);
        RbReplaceNode(node, x);
    } else {
        y = node->rb_right;
        while (y->rb_left != nullptr) {
            y = y->rb_left;
        }
        x              = y->rb_right;
        x_parent       = y;
        y_original_red = RbIsRed(y);
        // tty::printk("[RbErase] two children, y=%d (successor), x=%d\n",
        // y->GetPid(), x ? x->GetPid() : -1);

        if (x) {
            RbSetParent(x, y);
        }

        if (y->rb_parent == node) {
            if (x) {
                RbSetParent(x, y);
            }
        } else {
            RbReplaceNode(y, x);
            y->rb_right = node->rb_right;
            RbSetParent(node->rb_right, y);
        }

        RbReplaceNode(node, y);
        y->rb_left = node->rb_left;
        RbSetParent(node->rb_left, y);
        RbSetBlack(y);
        y->rb_is_red = node->rb_is_red;
    }

    // tty::printk("[RbErase] after replace: rb_root=%d\n",
    //             curr_sched.rb_root ? curr_sched.rb_root->GetPid() : -1);

    if (x && !y_original_red) {
        RbEraseColorFixup(x, x_parent);
    }

    if (curr_sched.rb_root) {
        Entity *leftmost = curr_sched.rb_root;
        // tty::printk("[RbErase] finding leftmost from root=%d\n",
        // leftmost->GetPid());
        while (leftmost->rb_left) {
            leftmost = leftmost->rb_left;
        }
        curr_sched.leftmost = leftmost;
        // tty::printk("[RbErase] leftmost=%d\n", leftmost->GetPid());
    } else {
        // tty::printk("[RbErase] WARNING: rb_root is nullptr!\n");
        curr_sched.leftmost = nullptr;
    }

    if (curr_sched.leftmost) {
        curr_sched.min_vruntime = curr_sched.leftmost->vruntime;
    } else {
        curr_sched.min_vruntime = 0;
    }

    // tty::printk("[RbErase] done: rb_root=%d, leftmost=%d\n",
    //             curr_sched.rb_root ? curr_sched.rb_root->GetPid() : -1,
    //             curr_sched.leftmost ? curr_sched.leftmost->GetPid() : -1);
}

}  // namespace cfs
