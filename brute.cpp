#include <iostream>
#include <queue>
#include <unordered_map>
#include <vector>
#include <algorithm>

using namespace std;

// Forward declaration
struct Node;
Node *buildTree(Node *root, int &numChildren, vector<string> &nodeLabels);

/**
 * @brief Represents a node in the M-ary tree.
 * * Only includes fields strictly necessary for lock status and tree structure.
 */
struct Node
{
    string label;
    vector<Node *> children;
    Node *parent;
    
    // Core lock status fields
    int userID;           // ID of the user who locked THIS node. 0 if unlocked.
    bool isLocked;        // True if THIS node is locked.

    Node(string name, Node *parentNode)
    {
        label = name;
        parent = parentNode;
        userID = 0;
        isLocked = false;
    }

    void addChildren(vector<string> childLabels, Node *parentNode)
    {
        for (auto &childLabel : childLabels)
        {
            children.push_back(new Node(childLabel, parentNode));
        }
    }
    
    // Helper function for memory cleanup (optional but good practice)
    ~Node() {
        for (Node* child : children) {
            delete child;
        }
    }
};

/**
 * @brief Manages the locking operations on the tree using a brute-force approach.
 */
class LockingTreeBruteForce
{
private:
    Node *root;
    unordered_map<string, Node *> labelToNode; // O(1) lookup of node by label.
    vector<string> outputLog;

public:
    LockingTreeBruteForce(Node *treeRoot) { 
        root = treeRoot; 
        fillLabelToNode(root); // Initialize map on construction
    }
    
    // Prevent copy/move operations for tree management
    LockingTreeBruteForce(const LockingTreeBruteForce&) = delete;
    LockingTreeBruteForce& operator=(const LockingTreeBruteForce&) = delete;
    
    // Destructor to clean up memory
    ~LockingTreeBruteForce() {
        // Only delete root if it exists, the Node destructor handles children
        if (root) delete root;
    }

    Node *getRoot() { return root; }

    /**
     * @brief Populates the labelToNode map using DFS.
     */
    void fillLabelToNode(Node *currentNode)
    {
        if (!currentNode) return;
        labelToNode[currentNode->label] = currentNode;
        for (auto child : currentNode->children)
            fillLabelToNode(child);
    }
    
    // --- BRUTE-FORCE HELPER FUNCTIONS ---

    /**
     * @brief Checks if any ancestor of currentNode is locked.
     * Time Complexity: O(H) (or O(N) in worst case for skew tree)
     */
    bool isAncestorLocked(Node *currentNode)
    {
        Node *current = currentNode->parent;
        while (current)
        {
            if (current->isLocked)
                return true;
            current = current->parent;
        }
        return false;
    }

    /**
     * @brief Checks if any descendant of currentNode is locked.
     * Time Complexity: O(Subtree Size) (or O(N) in worst case)
     */
    bool isDescendantLocked(Node *currentNode)
    {
        // BFS or DFS can be used here. DFS is simpler for recursion.
        for (auto child : currentNode->children)
        {
            if (child->isLocked)
                return true;
            if (isDescendantLocked(child))
                return true;
        }
        return false;
    }
    
    /**
     * @brief Checks descendants for upgrade conditions.
     * Traverses the subtree, returns false if any descendant is locked by a DIFFERENT user.
     * Collects all descendants locked by 'id' into 'lockedNodes'.
     * Time Complexity: O(Subtree Size) (or O(N) in worst case)
     */
    bool checkAndCollectDescendants(Node *currentNode, int id,
                                   vector<Node *> &lockedNodes)
    {
        bool result = true;
        
        if (currentNode->isLocked)
        {
            if (currentNode->userID != id)
                return false; // Found a lock by a different user
            lockedNodes.push_back(currentNode);
        }

        for (auto child : currentNode->children)
        {
            result = checkAndCollectDescendants(child, id, lockedNodes);
            if (!result) // Short-circuit
                return false;
        }

        return true;
    }

    // --- PUBLIC API ---

    /**
     * @brief Locks the node 'label' by user 'id'.
     * Time Complexity: O(N)
     */
    bool lockNode(string label, int id)
    {
        Node *targetNode = labelToNode[label];

        if (targetNode->isLocked) return false;
        
        // Brute-force checks
        if (isAncestorLocked(targetNode)) return false; // O(H)
        if (isDescendantLocked(targetNode)) return false; // O(Subtree Size)

        // Lock the node
        targetNode->isLocked = true;
        targetNode->userID = id;

        return true;
    }

    /**
     * @brief Unlocks the node 'label' by user 'id'.
     * Time Complexity: O(1)
     */
    bool unlockNode(string label, int id)
    {
        Node *targetNode = labelToNode[label];

        if (!targetNode->isLocked) return false;
        if (targetNode->userID != id) return false;

        // Unlock the node
        targetNode->isLocked = false;
        targetNode->userID = 0;

        return true;
    }

    /**
     * @brief Upgrades user 'id''s lock to node 'label'.
     * Time Complexity: O(N)
     */
    bool upgradeNode(string label, int id)
    {
        Node *targetNode = labelToNode[label];
        
        if (targetNode->isLocked) return false;
        
        // 1. Check if any ancestor is locked (Brute-force O(H))
        if (isAncestorLocked(targetNode)) return false;

        vector<Node *> lockedDescendants;
        
        // 2. Check and collect locked descendants (Brute-force O(Subtree Size))
        // Returns false if any descendant is locked by a DIFFERENT user.
        if (!checkAndCollectDescendants(targetNode, id, lockedDescendants))
            return false;
        
        // 3. Check if no descendant was locked (Fail condition 3)
        if (lockedDescendants.empty())
            return false;

        // Success: Unlock all valid descendants
        for (auto lockedDescendant : lockedDescendants)
        {
            // Note: The unlock in brute-force is O(1) as it only updates the node.
            // No counter updates are needed.
            lockedDescendant->isLocked = false;
            lockedDescendant->userID = 0;
        }

        // 4. Lock the target node
        targetNode->isLocked = true;
        targetNode->userID = id;

        return true;
    }

    /**
     * @brief Processes a list of queries.
     */
    void processQueries(vector<pair<int, pair<string, int>>> queries)
    {
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

    /**
     * @brief Prints the results of the queries.
     */
    void printOutputLog()
    {
        for (const string &result : outputLog)
        {
            cout << result << "\n";
        }
    }
};

// --- TREE BUILDING & MAIN FUNCTION (REMAIN UNCHANGED) ---

/**
 * @brief Builds the M-ary tree from a flat list of labels using BFS.
 */
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

/**
 * @brief Main function for I/O handling and execution.
 */
int main()
{
    ios_base::sync_with_stdio(false);
    cin.tie(NULL);

    int numNodes, numChildren, numQueries;
    if (!(cin >> numNodes >> numChildren >> numQueries)) return 0;

    vector<string> nodeLabels(numNodes);

    for (int i = 0; i < numNodes; i++)
        cin >> nodeLabels[i];

    // Build the tree
    Node *rootNode = new Node(nodeLabels[0], nullptr);
    rootNode = buildTree(rootNode, numChildren, nodeLabels);

    // Use the BruteForce class
    LockingTreeBruteForce lockingTree(rootNode);

    vector<pair<int, pair<string, int>>> queries(numQueries);

    for (int i = 0; i < numQueries; i++)
    {
        cin >> queries[i].first >> queries[i].second.first >>
             queries[i].second.second;
    }

    lockingTree.processQueries(queries);
    lockingTree.printOutputLog();
    
    // Memory cleanup is now handled by the destructor.
    
    return 0;
}