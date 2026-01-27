/**
 * @file rbtree.cc
 * @brief Red-Black Tree implementation for CFS
 * @author Kumosya, 2025-2026
 **/

#include <cstdint>

#include "kernel/cpu.h"
#include "kernel/task.h"
#include "kernel/tty.h"

namespace task::cfs {

static inline void RbSetRed(task::Pcb *node) {
    if (node) {
        node->se.rb_is_red = true;
    }
}

static inline void RbSetBlack(task::Pcb *node) {
    if (node) {
        node->se.rb_is_red = false;
    }
}

static inline bool RbIsRed(task::Pcb *node) {
    return node && node->se.rb_is_red;
}

static inline void RbSetParent(task::Pcb *node, task::Pcb *parent) {
    if (node) {
        node->se.rb_parent = parent;
    }
}

static void rb_print_node(task::Pcb *node, int depth, Pcb **visited,
                          int &visited_count, int max_visited) {
    if (!node) return;

    // 检测指针环：检查当前节点是否已经访问过
    for (int i = 0; i < visited_count; i++) {
        if (visited[i] == node) {
            tty::printk(" [LOOP DETECTED at node %d!]", node->pid);
            return;
        }
    }

    // 记录已访问节点
    if (visited_count < max_visited) {
        visited[visited_count++] = node;
    }

    tty::printk("%d[vr=%d,color=%c]", node->pid, node->se.vruntime,
                RbIsRed(node) ? 'R' : 'B');

    if (node->se.rb_left || node->se.rb_right) {
        tty::printk("->(");

        if (node->se.rb_left) {
            rb_print_node(node->se.rb_left, depth + 1, visited, visited_count,
                          max_visited);
            if (node->se.rb_right) {
                tty::printk(",");
                rb_print_node(node->se.rb_right, depth + 1, visited,
                              visited_count, max_visited);
            }
        } else if (node->se.rb_right) {
            tty::printk("nullptr,");
            rb_print_node(node->se.rb_right, depth + 1, visited, visited_count,
                          max_visited);
        }
        tty::printk(")");
    }
}

void Rq::RbPrintTree() {
    tty::printk("\n=== RBTree ===\n");
    tty::printk("root=%d, leftmost=%d\n",
                sched.rb_root ? sched.rb_root->pid : -1,
                sched.leftmost ? sched.leftmost->pid : -1);
    tty::printk("nr_running=%d, min_vruntime=%d\n", sched.nr_running,
                sched.min_vruntime);

    if (sched.rb_root) {
        tty::printk("Tree: ");
        // 防止指针环导致的无限递归，分配访问记录数组
        task::Pcb *visited[256];
        int visited_count = 0;
        rb_print_node(sched.rb_root, 0, visited, visited_count, 256);
        tty::printk("\n");
    }
    tty::printk("=============\n\n");
}

void Rq::RbLeftRotate(task::Pcb *x) {
    if (!x || !x->se.rb_right || x->se.rb_right == x) {  // 禁止自旋转
        // tty::printk("[LeftRotate] invalid: x=%d, right=%d (self rotate)\n",
        //             x ? x->pid : -1, x->se.rb_right ? x->se.rb_right->pid :
        //             -1);
        return;
    }
    task::Pcb *y = x->se.rb_right;
    if (!y) return;
    // tty::printk("[LeftRotate] x=%d, y=%d\n", x->pid, y->pid);

    x->se.rb_right = y->se.rb_left;

    if (y->se.rb_left != nullptr) {
        y->se.rb_left->se.rb_parent = x;
    }

    y->se.rb_parent = x->se.rb_parent;

    if (x->se.rb_parent == nullptr) {
        sched.rb_root = y;
    } else if (x == x->se.rb_parent->se.rb_left) {
        x->se.rb_parent->se.rb_left = y;
    } else {
        x->se.rb_parent->se.rb_right = y;
    }

    y->se.rb_left   = x;
    x->se.rb_parent = y;

    // tty::printk("[LeftRotate] done: x.parent=%d, y.parent=%d\n",
    //             x->se.rb_parent ? x->se.rb_parent->pid : -1,
    //             y->se.rb_parent ? y->se.rb_parent->pid : -1);
}

void Rq::RbRightRotate(task::Pcb *y) {
    if (!y || !y->se.rb_left || y->se.rb_left == y) {  // 禁止自旋转
        // tty::printk("[RightRotate] invalid: y=%d, left=%d (self rotate)\n",
        //             y ? y->pid : -1, y->se.rb_left ? y->se.rb_left->pid :
        //             -1);
        return;
    }
    task::Pcb *x = y->se.rb_left;
    if (!x) return;
    // tty::printk("[RightRotate] y=%d, x=%d\n", y->pid, x->pid);

    y->se.rb_left = x->se.rb_right;

    if (x->se.rb_right != nullptr) {
        x->se.rb_right->se.rb_parent = y;
    }

    x->se.rb_parent = y->se.rb_parent;

    if (y->se.rb_parent == nullptr) {
        sched.rb_root = x;
    } else if (y == y->se.rb_parent->se.rb_right) {
        y->se.rb_parent->se.rb_right = x;
    } else {
        y->se.rb_parent->se.rb_left = x;
    }

    x->se.rb_right  = y;
    y->se.rb_parent = x;

    // tty::printk("[RightRotate] done: x.parent=%d, y.parent=%d\n",
    //             x->se.rb_parent ? x->se.rb_parent->pid : -1,
    //             y->se.rb_parent ? y->se.rb_parent->pid : -1);
}

void Rq::RbInsertColorFixup(task::Pcb *node) {
    // tty::printk("[InsertFixup] node=%d start\n", node ? node->pid : -1);
    while (node && node->se.rb_parent && RbIsRed(node->se.rb_parent)) {
        task::Pcb *parent      = node->se.rb_parent;
        task::Pcb *grandparent = parent->se.rb_parent;
        if (!grandparent || parent == node || grandparent == parent ||
            grandparent == node) {
            // tty::printk("[InsertFixup] pointer loop detected, break\n");
            break;
        }
        if (parent == grandparent->se.rb_left) {
            task::Pcb *uncle = grandparent->se.rb_right;
            // tty::printk("[InsertFixup] parent=left, uncle=%d, is_red=%d\n",
            //             uncle ? uncle->pid : -1, uncle ? (int)RbIsRed(uncle)
            //             : -1);

            if (RbIsRed(uncle)) {
                // tty::printk("[InsertFixup] Case 1: uncle red, recolor\n");
                RbSetBlack(parent);
                RbSetBlack(uncle);
                RbSetRed(grandparent);
                node = grandparent;
            } else {
                if (node == parent->se.rb_right) {
                    // tty::printk("[InsertFixup] Case 2: node is right child,
                    // rotate\n");
                    node = parent;
                    RbLeftRotate(node);
                    parent = node->se.rb_parent;
                }

                // tty::printk("[InsertFixup] Case 3: recolor and rotate\n");
                RbSetBlack(parent);
                RbSetRed(grandparent);
                RbRightRotate(grandparent);
            }
        } else {
            task::Pcb *uncle = grandparent->se.rb_left;
            // tty::printk("[InsertFixup] parent=right, uncle=%d, is_red=%d\n",
            //             uncle ? uncle->pid : -1, uncle ? (int)RbIsRed(uncle)
            //             : -1);

            if (RbIsRed(uncle)) {
                // tty::printk("[InsertFixup] Case 1: uncle red, recolor\n");
                RbSetBlack(parent);
                RbSetBlack(uncle);
                RbSetRed(grandparent);
                node = grandparent;
            } else {
                if (node == parent->se.rb_left) {
                    // tty::printk("[InsertFixup] Case 2: node is left child,
                    // rotate\n");
                    node = parent;
                    RbRightRotate(node);
                    parent = node->se.rb_parent;
                }

                // tty::printk("[InsertFixup] Case 3: recolor and rotate\n");
                RbSetBlack(parent);
                RbSetRed(grandparent);
                RbLeftRotate(grandparent);
            }
        }
    }

    if (sched.rb_root) {
        RbSetBlack(sched.rb_root);
    }

    // tty::printk("[InsertFixup] done, root=%d\n", sched.rb_root ?
    // sched.rb_root->pid : -1);
}

void Rq::RbEraseColorFixup(task::Pcb *node, task::Pcb *parent) {
    // tty::printk("[EraseFixup] node=%d, parent=%d start\n",
    //             node ? node->pid : -1, parent ? parent->pid : -1);

    while (node != sched.rb_root && parent &&
           (node == nullptr || !RbIsRed(node))) {
        if (node == parent->se.rb_left) {
            task::Pcb *sibling = parent->se.rb_right;
            if (!sibling) break;

            // tty::printk("[EraseFixup] node=left, sibling=%d, is_red=%d\n",
            //             sibling ? sibling->pid : -1, sibling ?
            //             RbIsRed(sibling) : -1);

            if (RbIsRed(sibling)) {
                // tty::printk("[EraseFixup] sibling red, recolor and
                // rotate\n");
                RbSetBlack(sibling);
                RbSetRed(parent);
                RbLeftRotate(parent);
                sibling = parent->se.rb_right;
            }

            if ((sibling->se.rb_left == nullptr ||
                 !RbIsRed(sibling->se.rb_left)) &&
                (sibling->se.rb_right == nullptr ||
                 !RbIsRed(sibling->se.rb_right))) {
                // tty::printk("[EraseFixup] sibling children black, recolor
                // sibling\n");
                RbSetRed(sibling);
                node   = parent;
                parent = node->se.rb_parent;
            } else {
                if (sibling->se.rb_right == nullptr ||
                    !RbIsRed(sibling->se.rb_right)) {
                    // tty::printk("[EraseFixup] sibling right black, rotate
                    // sibling right\n");
                    if (sibling->se.rb_left) {
                        RbSetBlack(sibling->se.rb_left);
                    }
                    RbSetRed(sibling);
                    RbRightRotate(sibling);
                    sibling = parent->se.rb_right;
                }

                // tty::printk("[EraseFixup] recolor and rotate parent\n");
                RbSetBlack(sibling->se.rb_right);
                RbSetRed(parent);
                RbLeftRotate(parent);
                node = sched.rb_root;
                break;
            }
        } else {
            task::Pcb *sibling = parent->se.rb_left;
            if (!sibling) break;

            // tty::printk("[EraseFixup] node=right, sibling=%d, is_red=%d\n",
            //             sibling ? sibling->pid : -1, sibling ?
            //             RbIsRed(sibling) : -1);

            if (RbIsRed(sibling)) {
                // tty::printk("[EraseFixup] sibling red, recolor and
                // rotate\n");
                RbSetBlack(sibling);
                RbSetRed(parent);
                RbRightRotate(parent);
                sibling = parent->se.rb_left;
            }

            if ((sibling->se.rb_right == nullptr ||
                 !RbIsRed(sibling->se.rb_right)) &&
                (sibling->se.rb_left == nullptr ||
                 !RbIsRed(sibling->se.rb_left))) {
                // tty::printk("[EraseFixup] sibling children black, recolor
                // sibling\n");
                RbSetRed(sibling);
                node   = parent;
                parent = node->se.rb_parent;
            } else {
                if (sibling->se.rb_left == nullptr ||
                    !RbIsRed(sibling->se.rb_left)) {
                    // tty::printk("[EraseFixup] sibling left black, rotate
                    // sibling left\n");
                    if (sibling->se.rb_right) {
                        RbSetBlack(sibling->se.rb_right);
                    }
                    RbSetRed(sibling);
                    RbLeftRotate(sibling);
                    sibling = parent->se.rb_left;
                }

                // tty::printk("[EraseFixup] recolor and rotate parent\n");
                RbSetBlack(sibling->se.rb_left);
                RbSetRed(parent);
                RbRightRotate(parent);
                node = sched.rb_root;
                break;
            }
        }
    }

    if (node) {
        RbSetBlack(node);
    }

    // tty::printk("[EraseFixup] done\n");
}

void Rq::RbInitNode(task::Pcb *node) {
    if (!node) return;
    node->se.rb_left   = nullptr;
    node->se.rb_right  = nullptr;
    node->se.rb_parent = nullptr;
    node->se.rb_is_red = true;
}

void Rq::RbInsert(task::Pcb *node) {
    if (!node) return;

    // tty::printk("RbInsert: %d, vruntime: %d\n", node->pid,
    // node->se.vruntime);
    RbInitNode(node);

    task::Pcb *y = nullptr;
    task::Pcb *x = sched.rb_root;

    while (x != nullptr) {
        y = x;
        if (node->se.vruntime < x->se.vruntime) {
            x = x->se.rb_left;
        } else {
            x = x->se.rb_right;
        }
    }

    node->se.rb_parent = y;

    if (y == nullptr) {
        sched.rb_root = node;
        RbSetBlack(node);  // 根节点设为黑色
    } else if (node->se.vruntime < y->se.vruntime) {
        y->se.rb_left = node;
    } else {
        y->se.rb_right = node;
    }

    if (node->se.rb_parent == nullptr) {
        sched.leftmost = node;
        // tty::printk("leftmost: %d\n", sched.leftmost->pid);
    } else if (node->se.rb_parent->se.rb_parent != nullptr) {
        RbInsertColorFixup(node);
    }

    // 每次插入后重新计算全局leftmost
    task::Pcb *leftmost = sched.rb_root;
    if (leftmost) {
        while (leftmost->se.rb_left) {
            leftmost = leftmost->se.rb_left;
        }
        sched.leftmost     = leftmost;
        sched.min_vruntime = leftmost->se.vruntime;  // 更新min_vruntime
        // tty::printk("leftmost: %d\n", sched.leftmost->pid);
    }

    // RbPrintTree();
}

void Rq::RbReplaceNode(task::Pcb *u, task::Pcb *v) {
    // tty::printk("[ReplaceNode] u=%d, v=%d\n", u ? u->pid : -1, v ? v->pid :
    // -1);

    if (u->se.rb_parent == nullptr) {
        sched.rb_root = v;
    } else if (u == u->se.rb_parent->se.rb_left) {
        u->se.rb_parent->se.rb_left = v;
    } else {
        u->se.rb_parent->se.rb_right = v;
    }

    if (v != nullptr) {
        v->se.rb_parent = u->se.rb_parent;
    }
}

void Rq::RbErase(task::Pcb *node) {
    if (node == nullptr) {
        // tty::printk("[RbErase] ERROR: node is nullptr!\n");
        return;
    }

    // tty::printk("[RbErase] start: node=%d, rb_root=%d, nr_running=%d\n",
    //             node->pid,
    //             sched.rb_root ? sched.rb_root->pid : -1,
    //             sched.nr_running);

    task::Pcb *y        = node;
    task::Pcb *x        = nullptr;
    task::Pcb *x_parent = nullptr;
    bool y_original_red = RbIsRed(y);

    // tty::printk("[RbErase] node=%d, left=%d, right=%d, parent=%d\n",
    //             node->pid,
    //             node->rb_left ? node->rb_left->pid : -1,
    //             node->rb_right ? node->rb_right->pid : -1,
    //             node->rb_parent ? node->rb_parent->pid : -1);

    if (node->se.rb_left == nullptr) {
        x        = node->se.rb_right;
        x_parent = node->se.rb_parent;
        // tty::printk("[RbErase] left is nullptr, x=%d\n", x ? x->pid : -1);
        RbReplaceNode(node, x);
    } else if (node->se.rb_right == nullptr) {
        x        = node->se.rb_left;
        x_parent = node->se.rb_parent;
        // tty::printk("[RbErase] right is nullptr, x=%d\n", x ? x->pid : -1);
        RbReplaceNode(node, x);
    } else {
        y = node->se.rb_right;
        while (y->se.rb_left != nullptr) {
            y = y->se.rb_left;
        }
        x              = y->se.rb_right;
        x_parent       = y;
        y_original_red = RbIsRed(y);
        // tty::printk("[RbErase] two children, y=%d (successor), x=%d\n",
        // y->pid, x ? x->pid : -1);

        if (x) {
            RbSetParent(x, y);
        }

        if (y->se.rb_parent == node) {
            if (x) {
                RbSetParent(x, y);
            }
        } else {
            RbReplaceNode(y, x);
            y->se.rb_right = node->se.rb_right;
            RbSetParent(node->se.rb_right, y);
        }

        RbReplaceNode(node, y);
        y->se.rb_left = node->se.rb_left;
        RbSetParent(node->se.rb_left, y);
        RbSetBlack(y);
        y->se.rb_is_red = node->se.rb_is_red;
    }

    // tty::printk("[RbErase] after replace: rb_root=%d\n",
    //             sched.rb_root ? sched.rb_root->pid : -1);

    if (x && !y_original_red) {
        RbEraseColorFixup(x, x_parent);
    }

    if (sched.rb_root) {
        task::Pcb *leftmost = sched.rb_root;
        // tty::printk("[RbErase] finding leftmost from root=%d\n",
        // leftmost->pid);
        while (leftmost->se.rb_left) {
            leftmost = leftmost->se.rb_left;
        }
        sched.leftmost = leftmost;
        // tty::printk("[RbErase] leftmost=%d\n", leftmost->pid);
    } else {
        // tty::printk("[RbErase] WARNING: rb_root is nullptr!\n");
        sched.leftmost = nullptr;
    }

    if (sched.leftmost) {
        sched.min_vruntime = sched.leftmost->se.vruntime;
    } else {
        sched.min_vruntime = 0;
    }

    // tty::printk("[RbErase] done: rb_root=%d, leftmost=%d\n",
    //             sched.rb_root ? sched.rb_root->pid : -1,
    //             sched.leftmost ? sched.leftmost->pid : -1);
}

}  // namespace task::cfs
