#ifndef NODE_EDITOR_H
#define NODE_EDITOR_H

// Visual OpenCV pipeline: nodes from the palette are dropped into the editor,
// wired together, and the image reaching the Output node is shown in the
// Camera Output window.

#include "../dependencies/include/glad/glad.h"
#include "../dependencies/imgui/imgui.h"
#include "opencv_backend.h"
#include <memory>
#include <string>
#include <vector>

struct NodeState; // per-node runtime state (background model, cascade, ...), defined in the .cpp

struct GraphNode {
    int    id = 0;
    int    type = 0;
    ImVec2 pos{0, 0};              // top-left, in canvas coordinates
    ImVec2 size{0, 0};             // measured on the last frame
    int    inputs[2] = {-1, -1};   // id of the node feeding each input, -1 = unconnected
    std::vector<float> params;     // values of the node type's parameters

    // Layout of the pins, relative to pos (measured on the last frame)
    float  inPinY[2] = {0, 0};
    float  outPinY = 0;

    // Evaluation results
    cv::Mat     result;
    std::string info;              // e.g. "3 faces", shown under the parameters
    std::string error;
    double      ms = 0.0;
    bool        evaluated = false;
    std::shared_ptr<NodeState> state;
};

// A colored frame drawn behind nodes. Moving it moves the nodes inside it.
struct NodeGroup {
    int         id = 0;
    std::string title;
    ImVec2      pos{0, 0};           // canvas coordinates
    ImVec2      size{300, 200};
    int         color = 0;           // index into the group palette
    std::vector<int> fitTo;          // node ids to wrap once their sizes are known (then cleared)
};

struct NodeGraph {
    std::vector<std::unique_ptr<GraphNode>> nodes;  // draw order: last = on top
    std::vector<NodeGroup> groups;
    int    nextId = 1;
    int    nextGroupId = 1;
    int    selected = -1;            // primary selection (previewed, highlighted links)
    std::vector<int> selection;      // every selected node, including `selected`
    bool   previewSelected = false;  // show the selected node's image instead of the Output node's
    double totalMs = 0.0;

    // Image shown in the Pipeline Output window
    GLuint texture = 0;
    int    texW = 0, texH = 0;

    // View: screen = canvasOrigin + scroll + pos * zoom
    ImVec2 scroll{0, 0};
    float  zoom = 1.0f;
    bool   fitRequested = false;     // fit the view to the graph once every node has been measured
    bool   snap = true;              // snap dragged nodes to the grid
    bool   showMinimap = true;
    ImVec2 canvasCenter{300, 150};   // visible center, in canvas coordinates

    // Interaction state
    bool   draggingNodes = false;
    int    dragFromOutput = -1;      // node id whose output pin is being dragged
    int    dragToNode = -1, dragToInput = -1; // input pin being dragged towards an output
    bool   panning = false;
    float  panDistance = 0.0f;
    bool   boxSelecting = false;
    ImVec2 boxStart{0, 0};           // canvas coordinates
    int    draggingGroup = -1;
    std::vector<int> groupMembers;   // nodes moving with the dragged group
    int    resizingGroup = -1;
    int    renamingGroup = -1;
    bool   renameFocus = false;
    char   renameBuf[64] = "";
    bool   minimapDragging = false;
    ImVec2 minimapMin{0, 0}, minimapMax{0, 0}; // last frame's minimap rect (screen)
    int    hoverLinkNode = -1, hoverLinkInput = -1;

    // Context menu target
    ImVec2 contextPos{0, 0};
    int    contextNode = -1;
    int    contextGroup = -1;
    int    contextLinkNode = -1, contextLinkInput = -1;
};

void InitNodeGraph(NodeGraph& g);                              // Camera -> Output
void EvaluateNodeGraph(NodeGraph& g, const cv::Mat& frame);    // runs the graph and uploads the result
void DrawNodePalette(NodeGraph& g);
void DrawNodeEditor(NodeGraph& g);
void DestroyNodeGraph(NodeGraph& g);

#endif
