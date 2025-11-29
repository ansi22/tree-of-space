#include <iostream>
#include <queue>
#include <unordered_map>
#include <vector>
#include <atomic> // Used instead of mutex

using namespace std;

// Forward declaration for buildTree
struct Node;
Node *buildTree(Node *root, int &numChildren, vector<string> &nodeLabels);

/**
 * @brief Represents a node in the M-ary tree using atomic variables.
 */
struct Node
{
    string label;
    vector<Node *> children;
    Node *parent;
    
    // ATOMIC COUNTERS:
    std::atomic<int> ancestorLocked;
    std::atomic<int> descendantLocked;
    std::atomic<int> userID;
    std::atomic<bool> isLocked;

    Node(string name, Node *parentNode)
    {
        label = name;
        parent = parentNode;
        ancestorLocked = descendantLocked = userID = 0;
        isLocked = false;
    }
    // ... addChildren and destructor omitted for brevity ...
};

class LockingTreeLockFree
{
private:
    unordered_map<string, Node *> labelToNode; 
    // ... other members omitted ...

    // Lock-free updates still require traversing and updating ancestors/descendants.
    // This is the main point of contention.
    void updateDescendant(Node *currentNode, int value)
    {
        for (auto child : currentNode->children)
        {
            // Simple atomic modification. Safe for a single variable update.
            child->ancestorLocked += value; 
            updateDescendant(child, value);
        }
    }
    
public:
    // ... Constructor and helpers omitted ...

    /**
     * @brief Locks the node 'label' by user 'id' using optimistic locking.
     * The CAS loop is used only for the *target* node's lock state.
     * Ancestor/descendant updates are still problematic.
     */
    bool lockNode(string label, int id)
    {
        Node *targetNode = labelToNode[label];

        // 1. Initial Check (Racy, but we rely on CAS later)
        if (targetNode->isLocked.load() || 
            targetNode->ancestorLocked.load() != 0 || 
            targetNode->descendantLocked.load() != 0)
        {
            return false;
        }
        
        // 2. Attempt to Lock the Target Node using CAS (The ONLY fully safe step)
        bool expected_isLocked = false; // Expecting the node to be unlocked
        bool desired_isLocked = true;   // Wanting to set it to locked

        // CAS Loop: Attempts to set isLocked to true ONLY IF it is currently false.
        // If it fails (another thread locked it), the function returns false.
        if (!targetNode->isLocked.compare_exchange_strong(expected_isLocked, desired_isLocked))
        {
            return false; 
        }

        // If CAS succeeded, the node is locked! Now update the counters.
        targetNode->userID.store(id);

        // 3. Update Ancestors (O(H)) - Still racy without per-node CAS/locks
        Node *currentNode = targetNode->parent;
        while (currentNode)
        {
            // Simple atomic increment: safe for the single counter value.
            currentNode->descendantLocked++;
            currentNode = currentNode->parent;
        }

        // 4. Update Descendants (O(Subtree Size)) - Still racy
        updateDescendant(targetNode, 1);
        
        return true;
    }
    
    // ... unlockNode and upgradeNode would face similar complexity ...
};