#include <iostream>
#include <queue>
#include <unordered_map>
#include <vector>
#include <algorithm> // for std::min

using namespace std;

// Forward declaration for buildTree
struct Node;
Node *buildTree(Node *root, int &numChildren, vector<string> &nodeLabels);

/**
 * @brief Represents a node in the M-ary tree.
 * Includes fields for tracking lock status and ancestor/descendant lock counts.
 */
struct Node
{
    string label;
    vector<Node *> children;
    Node *parent;
    // Counters for optimization:
    int ancestorLocked;   // > 0 if any ancestor is locked by ANY user.
    int descendantLocked; // > 0 if any descendant is locked by ANY user.
    int userID;           // ID of the user who locked THIS node.
    bool isLocked;        // True if THIS node is locked.

    Node(string name, Node *parentNode)
    {
        label = name;
        parent = parentNode;
        ancestorLocked = descendantLocked = userID = 0;
        isLocked = false;
    }

    /**
     * @brief Adds new children nodes.
     * FIX: Correctly uses 'this' as the parent pointer for the new children.
     */
    void addChildren(vector<string> childLabels)
    {
        for (auto &childLabel : childLabels)
        {
            children.push_back(new Node(childLabel, this)); 
        }
    }
    
    // Simple destructor for memory cleanup (optional, but good practice)
    ~Node() {
        for (Node* child : children) {
            delete child;
        }
    }
};

/**
 * @brief Manages the locking operations on the tree.
 */
class LockingTree
{
private:
    Node *root;
    unordered_map<string, Node *> labelToNode; // O(1) lookup of node by label.
    vector<string> outputLog;

public:
    LockingTree(Node *treeRoot) { root = treeRoot; }
    Node *getRoot() { return root; }

    /**
     * @brief Populates the labelToNode map using DFS.
     */
    void fillLabelToNode(Node *currentNode)
    {
        if (!currentNode)
            return;
        labelToNode[currentNode->label] = currentNode;
        for (auto child : currentNode->children)
            fillLabelToNode(child);
    }

    /**
     * @brief Updates the ancestorLocked counter for all descendants of currentNode.
     */
    void updateDescendant(Node *currentNode, int value)
    {
        for (auto child : currentNode->children)
        {
            child->ancestorLocked += value;
            updateDescendant(child, value);
        }
    }

    /**
     * @brief Checks if all locked descendants are locked by the same user 'id' and collects them.
     */
    bool checkDescendantsLocked(Node *currentNode, int &id,
                                vector<Node *> &lockedNodes)
    {
        if (currentNode->isLocked)
        {
            if (currentNode->userID != id)
                return false;
            lockedNodes.push_back(currentNode);
        }

        // Optimization: Stop if no descendants or node itself is locked.
        if (currentNode->descendantLocked == 0 && !currentNode->isLocked)
            return true;

        bool result = true;

        for (auto child : currentNode->children)
        {
            result &= checkDescendantsLocked(child, id, lockedNodes);
            if (!result)
                return false;
        }

        return result;
    }

    /**
     * @brief Locks the node 'label' by user 'id'.
     */
    bool lockNode(string label, int id)
    {
        Node *targetNode = labelToNode[label];

        if (targetNode->isLocked)
            return false;

        if (targetNode->ancestorLocked != 0 || targetNode->descendantLocked != 0)
            return false;

        // 1. Update ancestors (Upward traversal)
        Node *currentNode = targetNode->parent;
        while (currentNode)
        {
            currentNode->descendantLocked++;
            currentNode = currentNode->parent;
        }

        // 2. Update descendants (Downward traversal)
        updateDescendant(targetNode, 1);
        
        // 3. Lock the node
        targetNode->isLocked = true;
        targetNode->userID = id;

        return true;
    }

    /**
     * @brief Unlocks the node 'label' by user 'id'.
     */
    bool unlockNode(string label, int id)
    {
        Node *targetNode = labelToNode[label];

        if (!targetNode->isLocked)
            return false;

        if (targetNode->isLocked && targetNode->userID != id)
            return false;

        // 1. Update ancestors (Upward traversal)
        Node *currentNode = targetNode->parent;
        while (currentNode)
        {
            currentNode->descendantLocked--;
            currentNode = currentNode->parent;
        }

        // 2. Update descendants (Downward traversal)
        updateDescendant(targetNode, -1);
        
        // 3. Unlock the node
        targetNode->isLocked = false;
        targetNode->userID = 0;

        return true;
    }

    /**
     * @brief Upgrades user 'id''s lock to node 'label'.
     */
    bool upgradeNode(string label, int id)
    {
        Node *targetNode = labelToNode[label];

        if (targetNode->isLocked)
            return false;

        if (targetNode->ancestorLocked != 0 || targetNode->descendantLocked == 0)
            return false;

        vector<Node *> lockedDescendants;

        if (checkDescendantsLocked(targetNode, id, lockedDescendants))
        {
            for (auto lockedDescendant : lockedDescendants)
            {
                // Unlocking descendants, which updates the descendant/ancestor counters
                if (!unlockNode(lockedDescendant->label, id))
                {
                    return false;
                }
            }
        }
        else
            return false; 

        // Lock the target node (descendantLocked is now 0)
        lockNode(label, id); 

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
    
    // Destructor to ensure proper memory cleanup for the tree
    ~LockingTree() {
        if (root) {
            delete root;
        }
    }
};

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

        // Call addChildren with the correct signature
        currentNode->addChildren(tempChildrenLabels); 
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
    // Fast I/O
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
    
    // The LockingTree destructor handles deleting the nodes via 'delete rootNode'
    
    return 0;
}