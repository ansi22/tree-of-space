#include <iostream>
#include <queue>
#include <unordered_map>
#include <vector>
#include <mutex> // Required for thread safety
#include <algorithm>

using namespace std;

// Forward declaration
struct Node;
Node *buildTree(Node *root, int &numChildren, vector<string> &nodeLabels);

/**
 * @brief Represents a node in the M-ary tree.
 */
struct Node
{
    string label;
    vector<Node *> children;
    Node *parent;
    
    // Counters for optimization:
    int ancestorLocked;     // > 0 if any ancestor is locked by ANY user.
    int descendantLocked;   // > 0 if any descendant is locked by ANY user.
    int userID;             // ID of the user who locked THIS node.
    bool isLocked;          // True if THIS node is locked.

    Node(string name, Node *parentNode)
    {
        label = name;
        parent = parentNode;
        ancestorLocked = descendantLocked = userID = 0;
        isLocked = false;
    }

    void addChildren(vector<string> childLabels, Node *parentNode)
    {
        for (auto &childLabel : childLabels)
        {
            children.push_back(new Node(childLabel, parentNode));
        }
    }
    
    // Destructor for cleanup (added for completeness)
    ~Node() {
        for (Node* child : children) {
            delete child;
        }
    }
};

/**
 * @brief Manages the locking operations on the tree (Thread-Safe version).
 */
class LockingTree
{
private:
    Node *root;
    unordered_map<string, Node *> labelToNode; // O(1) lookup of node by label.
    vector<string> outputLog;
    
    // MUTEX: Global mutex to protect all tree modifications and reads.
    std::mutex tree_mutex; 

public:
    LockingTree(Node *treeRoot) { root = treeRoot; }
    
    // Destructor to clean up memory
    ~LockingTree() {
        if (root) delete root;
    }
    
    Node *getRoot() { return root; }

    // --- Helper functions remain UNCHANGED (they are only called while a lock is held) ---

    void fillLabelToNode(Node *currentNode)
    {
        if (!currentNode) return;
        labelToNode[currentNode->label] = currentNode;
        for (auto child : currentNode->children)
            fillLabelToNode(child);
    }

    void updateDescendant(Node *currentNode, int value)
    {
        for (auto child : currentNode->children)
        {
            child->ancestorLocked += value;
            updateDescendant(child, value);
        }
    }

    bool checkDescendantsLocked(Node *currentNode, int &id,
                                vector<Node *> &lockedNodes)
    {
        if (currentNode->isLocked)
        {
            if (currentNode->userID != id) return false;
            lockedNodes.push_back(currentNode);
        }

        if (currentNode->descendantLocked == 0 && !currentNode->isLocked)
            return true;

        bool result = true;

        for (auto child : currentNode->children)
        {
            result &= checkDescendantsLocked(child, id, lockedNodes);
            if (!result) return false;
        }

        return true;
    }
    
    // --- Public API with Mutex Protection ---

    bool lockNode(string label, int id)
    {
        // Use lock_guard for RAII: lock the mutex, and automatically unlock on return/exit.
        std::lock_guard<std::mutex> lock(tree_mutex); 
        
        // The rest of the logic is the same as the original code
        Node *targetNode = labelToNode[label];

        if (targetNode->isLocked) return false;
        if (targetNode->ancestorLocked != 0 || targetNode->descendantLocked != 0) return false;

        // 1. Update ancestors (O(H))
        Node *currentNode = targetNode->parent;
        while (currentNode)
        {
            currentNode->descendantLocked++;
            currentNode = currentNode->parent;
        }

        // 2. Update descendants (O(Subtree Size))
        updateDescendant(targetNode, 1);
        
        // 3. Lock the node
        targetNode->isLocked = true;
        targetNode->userID = id;

        return true;
        // Lock is automatically released by lock_guard here.
    }

    bool unlockNode(string label, int id)
    {
        std::lock_guard<std::mutex> lock(tree_mutex);
        
        Node *targetNode = labelToNode[label];

        if (!targetNode->isLocked) return false;
        if (targetNode->userID != id) return false;

        // 1. Update ancestors (O(H))
        Node *currentNode = targetNode->parent;
        while (currentNode)
        {
            currentNode->descendantLocked--;
            currentNode = currentNode->parent;
        }

        // 2. Update descendants (O(Subtree Size))
        updateDescendant(targetNode, -1);
        
        // 3. Unlock the node
        targetNode->isLocked = false;
        targetNode->userID = 0; // Reset userID

        return true;
    }

    bool upgradeNode(string label, int id)
    {
        std::lock_guard<std::mutex> lock(tree_mutex);

        Node *targetNode = labelToNode[label];

        if (targetNode->isLocked) return false;
        if (targetNode->ancestorLocked != 0 || targetNode->descendantLocked == 0) return false;

        vector<Node *> lockedDescendants;

        if (checkDescendantsLocked(targetNode, id, lockedDescendants))
        {
            // Unlock all valid descendants. Since the lock_guard is active,
            // the internal calls to unlockNode are safe.
            for (auto lockedDescendant : lockedDescendants)
            {
                // This call effectively performs the *internal* unlock steps (counter updates)
                // but doesn't need to re-acquire the global mutex because we already hold it.
                // We must use the unlocked logic directly or ensure the helper function
                // is structured to skip mutex acquisition when called internally.
                // For simplicity and correctness with the existing unlockNode, 
                // we'll rewrite the unlock logic here to avoid deadlocks/re-locking.
                
                // Directly perform unlock logic (since we hold the lock):
                Node *descendantNode = lockedDescendant;

                // 1. Update ancestors 
                Node *currentParent = descendantNode->parent;
                while (currentParent)
                {
                    currentParent->descendantLocked--;
                    currentParent = currentParent->parent;
                }

                // 2. Update descendants
                updateDescendant(descendantNode, -1);
                
                // 3. Unlock the node
                descendantNode->isLocked = false;
                descendantNode->userID = 0;
            }
        }
        else
            return false; // Failed the descendant check

        // Now that all descendants are unlocked and counters updated:
        // Lock the target node (perform lock logic directly)
        
        // No need to re-check conditions as they were met for upgrade.
        
        // 1. Update ancestors (O(H))
        Node *currentNode = targetNode->parent;
        while (currentNode)
        {
            currentNode->descendantLocked++;
            currentNode = currentNode->parent;
        }

        // 2. Update descendants (O(Subtree Size))
        updateDescendant(targetNode, 1);
        
        // 3. Lock the node
        targetNode->isLocked = true;
        targetNode->userID = id;

        return true;
    }
    
    // --- Query Processing (Needs Lock if accessing the log) ---

    void processQueries(vector<pair<int, pair<string, int>>> queries)
    {
        // Locking the entire query process is fine since it's only writing to the outputLog,
        // and the main operations already handle tree locking internally.
        // We will stick to the original structure, letting the main functions handle locks.
        
        for (auto query : queries)
        {
            int opcode = query.first;
            string nodeLabel = query.second.first;
            int userId = query.second.second;

            switch (opcode)
            {
            case 1:
                lockNode(nodeLabel, userId) ? outputLog.push_back("true")
                                           : outputLog.push_back("false");
                break;
            case 2:
                unlockNode(nodeLabel, userId) ? outputLog.push_back("true")
                                              : outputLog.push_back("false");
                break;
            case 3:
                upgradeNode(nodeLabel, userId) ? outputLog.push_back("true")
                                               : outputLog.push_back("false");
                break;
            }
        }
    }

    void printOutputLog()
    {
        // No lock needed here if called after processQueries, 
        // as no concurrent modification is expected.
        for (const string &result : outputLog)
        {
            cout << result << "\n";
        }
    }
};

// --- buildTree and main() remain UNCHANGED ---

Node *buildTree(Node *root, int &numChildren, vector<string> &nodeLabels)
{
    queue<Node *> q;
    q.push(root);

    int startIndex = 1;

    while (!q.empty() && startIndex < nodeLabels.size())
    {
        Node *currentNode = q.front();
        q.pop();

        vector<string> tempChildrenLabels;
        
        int endIndex = min((int)nodeLabels.size(), startIndex + numChildren);
        for (int i = startIndex; i < endIndex; i++)
            tempChildrenLabels.push_back(nodeLabels[i]);

        currentNode->addChildren(tempChildrenLabels, currentNode);
        startIndex += numChildren;

        for (auto child : currentNode->children)
            q.push(child);
    }

    return root;
}

int main()
{
    ios_base::sync_with_stdio(false);
    cin.tie(NULL);

    int numNodes, numChildren, numQueries;
    if (!(cin >> numNodes >> numChildren >> numQueries)) return 0;

    vector<string> nodeLabels(numNodes);

    for (int i = 0; i < numNodes; i++)
        cin >> nodeLabels[i];

    Node *rootNode = new Node(nodeLabels[0], nullptr);
    rootNode = buildTree(rootNode, numChildren, nodeLabels);

    LockingTree lockingTree(rootNode);
    lockingTree.fillLabelToNode(lockingTree.getRoot());

    vector<pair<int, pair<string, int>>> queries(numQueries);

    for (int i = 0; i < numQueries; i++)
    {
        cin >> queries[i].first >> queries[i].second.first >>
             queries[i].second.second;
    }

    lockingTree.processQueries(queries);
    lockingTree.printOutputLog();
    
    // The LockingTree destructor handles memory cleanup now.
    
    return 0;
}