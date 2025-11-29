#include <iostream>
#include <queue>
#include <unordered_map>
#include <vector>
#include <string>
#include <algorithm>
// No standard concurrency headers included

using namespace std;

// ----------------------------------------------------------------------
// 1. CUSTOM SYNCHRONIZATION IMPLEMENTATION (Spinlock)
// ----------------------------------------------------------------------

/**
 * @brief Custom SpinLock implementation using compiler intrinsics.
 * * WARNING: This relies on compiler-specific built-in functions 
 * (__sync_lock_test_and_set, __sync_lock_release) which provide atomic 
 * guarantees similar to std::atomic, but are external to the standard C++ 
 * synchronization headers. This code is NOT portable across all C++ compilers 
 * without these specific built-ins.
 */
class CustomSpinLock {
private:
    // 0 = unlocked, 1 = locked. 'volatile' prevents caching/optimization.
    volatile int lock_flag = 0; 

public:
    /**
     * @brief Acquires the lock atomically using an intrinsic test-and-set.
     */
    void lock() {
        // __sync_lock_test_and_set atomically sets lock_flag to 1 and returns 
        // the previous value. We loop while the previous value was 1 (locked).
        while (__sync_lock_test_and_set(&lock_flag, 1)) {
            // Spin: wait for the flag to be released
        }
    }

    /**
     * @brief Releases the lock atomically using an intrinsic release.
     */
    void unlock() {
        // __sync_lock_release atomically sets lock_flag to 0.
        __sync_lock_release(&lock_flag);
    }
};

// ----------------------------------------------------------------------
// 2. TREE STRUCTURE AND INITIALIZATION
// ----------------------------------------------------------------------

/**
 * @brief Builds the M-ary tree structure using vectors to store parent/child relationships.
 * @param numChildren The maximum branching factor.
 * @param nodeLabels The list of all node labels.
 * @param parentID Output vector storing parent index for each node.
 * @param childrenIDs Output vector of vectors storing children indices for each node.
 * @param labelToID Output map from label string to node index.
 * @param idToLabel Output vector storing label string for each node index.
 * @return The index of the root node (always 0).
 */
int buildTree(int numChildren, const vector<string>& nodeLabels,
              vector<int>& parentID, vector<vector<int>>& childrenIDs,
              unordered_map<string, int>& labelToID, vector<string>& idToLabel) {
    
    int numNodes = nodeLabels.size();
    parentID.assign(numNodes, -1);
    childrenIDs.assign(numNodes, vector<int>());
    idToLabel = nodeLabels;

    labelToID[nodeLabels[0]] = 0;
    queue<int> q;
    q.push(0);

    int startIndex = 1; 

    while (!q.empty() && startIndex < numNodes) {
        int parentIndex = q.front();
        q.pop();

        int endIndex = min(numNodes, startIndex + numChildren);
        
        for (int i = startIndex; i < endIndex; i++) {
            int childIndex = i;
            
            labelToID[nodeLabels[childIndex]] = childIndex;
            parentID[childIndex] = parentIndex;
            childrenIDs[parentIndex].push_back(childIndex);
            
            q.push(childIndex);
        }
        startIndex = endIndex;
    }

    return 0; // Root is always at index 0
}


// ----------------------------------------------------------------------
// 3. LOCKING TREE IMPLEMENTATION
// ----------------------------------------------------------------------

class LockingTree {
private:
    // Shared state (protected by lock_guard)
    vector<int> parentID;
    vector<vector<int>> childrenIDs;
    vector<int> ancestorLockedCount;
    vector<int> descendantLockedCount;
    vector<int> currentUserID;
    vector<bool> isNodeLocked;
    unordered_map<string, int> labelToID;
    vector<string> idToLabel;
    
    vector<string> outputLog;
    int rootIndex;
    
    // Custom Lock object to protect the shared state
    CustomSpinLock lock_guard; 

    /**
     * @brief Finds the index of a node from its label.
     */
    int getIndex(const string& label) {
        auto it = labelToID.find(label);
        return (it != labelToID.end()) ? it->second : -1;
    }

    /**
     * @brief Updates the ancestorLockedCount for the entire subtree (recursive helper).
     */
    void updateDescendant(int nodeIndex, int value) {
        for (int childIndex : childrenIDs[nodeIndex]) {
            ancestorLockedCount[childIndex] += value;
            updateDescendant(childIndex, value);
        }
    }

    /**
     * @brief Checks if all locked descendants of nodeIndex are locked by user 'id'.
     * Collects all such locked nodes into lockedNodes.
     */
    bool checkDescendantsLocked(int nodeIndex, int id, vector<int>& lockedNodes) {
        // If this node is locked, check the user ID
        if (isNodeLocked[nodeIndex]) {
            if (currentUserID[nodeIndex] != id) return false;
            lockedNodes.push_back(nodeIndex);
        }
        
        // Optimization: if this node is not locked and has no locked descendants, stop
        if (descendantLockedCount[nodeIndex] == 0 && !isNodeLocked[nodeIndex]) return true;

        // Recurse on children
        for (int childIndex : childrenIDs[nodeIndex]) {
            if (!checkDescendantsLocked(childIndex, id, lockedNodes)) return false;
        }
        return true;
    }

public:
    LockingTree(int numNodes, int numChildren, const vector<string>& nodeLabels) {
        // Initialize structure and state vectors
        vector<int> temp_parentID;
        vector<vector<int>> temp_childrenIDs;
        unordered_map<string, int> temp_labelToID;
        vector<string> temp_idToLabel;
        
        rootIndex = buildTree(numChildren, nodeLabels, temp_parentID, temp_childrenIDs, temp_labelToID, temp_idToLabel);
        
        parentID = move(temp_parentID);
        childrenIDs = move(temp_childrenIDs);
        labelToID = move(temp_labelToID);
        idToLabel = move(temp_idToLabel);

        ancestorLockedCount.assign(numNodes, 0);
        descendantLockedCount.assign(numNodes, 0);
        currentUserID.assign(numNodes, 0);
        isNodeLocked.assign(numNodes, false);
    }
    
    /**
     * @brief Attempts to lock the node.
     */
    bool lockNode(const string& label, int id) {
        lock_guard.lock();
        
        // --- CRITICAL SECTION START ---
        int targetIndex = getIndex(label);
        if (targetIndex == -1) { lock_guard.unlock(); return false; }

        if (isNodeLocked[targetIndex] || 
            ancestorLockedCount[targetIndex] != 0 || 
            descendantLockedCount[targetIndex] != 0) {
            
            lock_guard.unlock(); 
            return false;
        }

        // State modification
        int current = parentID[targetIndex];
        while (current != -1) {
            descendantLockedCount[current]++;
            current = parentID[current];
        }
        updateDescendant(targetIndex, 1);
        isNodeLocked[targetIndex] = true;
        currentUserID[targetIndex] = id;
        
        // --- CRITICAL SECTION END ---
        lock_guard.unlock(); 
        return true;
    }

    /**
     * @brief Attempts to unlock the node.
     */
    bool unlockNode(const string& label, int id) {
        lock_guard.lock();
        
        // --- CRITICAL SECTION START ---
        int targetIndex = getIndex(label);
        if (targetIndex == -1) { lock_guard.unlock(); return false; }

        if (!isNodeLocked[targetIndex] || currentUserID[targetIndex] != id) {
            lock_guard.unlock();
            return false;
        }

        // State modification
        int current = parentID[targetIndex];
        while (current != -1) {
            descendantLockedCount[current]--;
            current = parentID[current];
        }
        updateDescendant(targetIndex, -1);
        isNodeLocked[targetIndex] = false;
        currentUserID[targetIndex] = 0;
        
        // --- CRITICAL SECTION END ---
        lock_guard.unlock();
        return true;
    }

    /**
     * @brief Attempts to upgrade the lock on the node.
     */
    bool upgradeNode(const string& label, int id) {
        lock_guard.lock();
        
        // --- CRITICAL SECTION START ---
        int targetIndex = getIndex(label);
        if (targetIndex == -1) { lock_guard.unlock(); return false; }

        if (isNodeLocked[targetIndex] || ancestorLockedCount[targetIndex] != 0 || descendantLockedCount[targetIndex] == 0) {
            lock_guard.unlock();
            return false;
        }

        vector<int> lockedDescendants;
        if (checkDescendantsLocked(targetIndex, id, lockedDescendants)) {
            // Unlock all valid descendants (critical section holds the lock)
            for (int lockedIndex : lockedDescendants) {
                // Perform unlock logic inline
                int current = parentID[lockedIndex];
                while (current != -1) {
                    descendantLockedCount[current]--;
                    current = parentID[current];
                }
                updateDescendant(lockedIndex, -1);
                isNodeLocked[lockedIndex] = false;
                currentUserID[lockedIndex] = 0; 
            }
        } else {
            lock_guard.unlock();
            return false;
        }

        // Lock the target node (critical section holds the lock)
        isNodeLocked[targetIndex] = true;
        currentUserID[targetIndex] = id;

        // Propagate lock changes for the new lock on targetIndex
        int current = parentID[targetIndex];
        while (current != -1) {
            descendantLockedCount[current]++;
            current = parentID[current];
        }
        updateDescendant(targetIndex, 1);
        
        // --- CRITICAL SECTION END ---
        lock_guard.unlock();
        return true;
    }

    /**
     * @brief Processes a list of queries sequentially.
     */
    void processQueries(const vector<pair<int, pair<string, int>>>& queries) {
        for (const auto& query : queries) {
            int opcode = query.first;
            const string& nodeLabel = query.second.first;
            int userId = query.second.second;

            bool result = false;
            switch (opcode) {
                case 1: result = lockNode(nodeLabel, userId); break;
                case 2: result = unlockNode(nodeLabel, userId); break;
                case 3: result = upgradeNode(nodeLabel, userId); break;
            }
            // Writing to outputLog needs to be protected if processQueries were multithreaded,
            // but assuming single-threaded query processing, a simple push_back is fine.
            outputLog.push_back(result ? "true" : "false");
        }
    }

    /**
     * @brief Prints the final output log.
     */
    void printOutputLog() {
        for (const string& result : outputLog) {
            cout << result << "\n";
        }
    }
};

// ----------------------------------------------------------------------
// 4. MAIN EXECUTION
// ----------------------------------------------------------------------

int main() {
    // Standard fast I/O setup
    ios_base::sync_with_stdio(false);
    cin.tie(NULL);

    int numNodes, numChildren, numQueries;
    if (!(cin >> numNodes >> numChildren >> numQueries)) return 0;

    vector<string> nodeLabels(numNodes);
    for (int i = 0; i < numNodes; i++) {
        cin >> nodeLabels[i];
    }
    
    // Initialize tree and logic
    LockingTree lockingTree(numNodes, numChildren, nodeLabels);

    // Read queries
    vector<pair<int, pair<string, int>>> queries(numQueries);
    for (int i = 0; i < numQueries; i++) {
        cin >> queries[i].first >> queries[i].second.first >> queries[i].second.second;
    }

    // Process and output results
    lockingTree.processQueries(queries);
    lockingTree.printOutputLog();
    
    return 0;
}