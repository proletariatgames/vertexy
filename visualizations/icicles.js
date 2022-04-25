const width = 400;
var height = 100;
const segmentX = d => (d.x0);
const segmentY = d => (d.y0);
const segmentWidth = d => (d.x1 - d.x0);
const segmentHeight = d => (d.y1 - d.y0);
const breadcrumbWidth = 100;
const breadcrumbHeight = 30;

var csv;
var data;
var icicle = { sequence: [], percentage: 0.0, varValue: "" };
var deepestLevel;

// Runs when a file is uploaded via the button
function onFileUpload()
{
    var fr=new FileReader();
    fr.onload=function(){
        initializeData(fr.result);
    }
    fr.readAsText(this.files[0]);
}

// Initializes data based on the text of the uploaded file
function initializeData(text)
{
    csv = d3.csvParseRows(text);
    data = buildHierarchy(csv);

    drawBreadcrumbs(".breadcrumb-container");
    drawIcicles(".icicle-container");
}

// Generates a color based on a string input
function getColor(name)
{
  var hash = 0;
  for (var i = 0; i < name.length; i++) {
    hash = name.charCodeAt(i) + ((hash << 5) - hash);
  }
  var colour = '#';
  for (var i = 0; i < 3; i++) {
    var value = (hash >> (i * 8)) & 0xFF;
    let colorString = ('00' + value.toString(16));
    colour += colorString.substring(colorString.length-2);
  }
  return colour;
}

// Builds the decision hierarchy from the inputted csv
function buildHierarchy(csv)
{
    // Helper function that transforms the given CSV into a hierarchical format.
    const root = { name: "root", value: 0, children: [] };
    let currentHierarchy = [];
    currentHierarchy.push(root);
    deepestLevel = 0;
    
    for (let i = 0; i < csv.length; i++) {
      // Extract csv vars
      const decisionLevel = csv[i][0];
      const varName = csv[i][1];
      const constraintID = csv[i][2];
      const varValue = csv[i][3];
  
      // If this is a propagated row, we're ignoring it for now
      if (constraintID != "-1" || decisionLevel <= 0)
      {
        continue;
      }
  
      // Create a node to fit into the hierarchical format
      let childNode = { name: varName, level: decisionLevel, constraint: constraintID, varValue: varValue, value: 0, children: [] };
  
      // Parent this based on its level
      let parentNode = currentHierarchy[childNode.level-1];
      parentNode.children.push(childNode);
  
      // Update the current hierarchy
      while (currentHierarchy[currentHierarchy.length - 1] != parentNode)
      {
        // If we backtrack, update the value of leaf nodes while we're here
        const poppedNode = currentHierarchy.pop();
        if (poppedNode.children.length == 0)
        {
          poppedNode.value = 1;
        }
      }
      currentHierarchy.push(childNode);

      // Update deepest level for breadcrumb vis
      if (parseInt(deepestLevel) < parseInt(decisionLevel))
      {
          deepestLevel = decisionLevel;
      }
    }
  
    // Update the value of the final leaf node
    const poppedNode = currentHierarchy.pop();
    poppedNode.value = 1;

    // Update the height based on the deepest level
    height = deepestLevel * 5;
    
    return root;
  }

  
// Generate a string that describes the points of a breadcrumb SVG polygon.
function breadcrumbPoints(d, i)
{
    const tipWidth = 10;
    const points = [];
    points.push("0,0");
    points.push(`${breadcrumbWidth},0`);
    points.push(`${breadcrumbWidth + tipWidth},${breadcrumbHeight / 2}`);
    points.push(`${breadcrumbWidth},${breadcrumbHeight}`);
    points.push(`0,${breadcrumbHeight}`);
    if (i > 0) {
      // Leftmost breadcrumb; don't include 6th vertex.
      points.push(`${tipWidth},${breadcrumbHeight / 2}`);
    }
    return points.join(" ");
  }

  
partition = data =>
  d3
    .partition()
    .padding(1)
    .size([width, height])(
    d3
      .hierarchy(data)
      .sum(d => d.value)
  );

// Fills the breadcrumb container with the currently-viewed icicle
function drawBreadcrumbs(container)
{
    d3.select(container).selectAll("*").remove();
    const breadcrumbWindowHeight = breadcrumbHeight * (1 + parseInt(deepestLevel / 9)) + 20;

    const svg = d3
      .select(container).append("svg")
      .attr("viewBox", `0 0 ${breadcrumbWidth * 10} ${breadcrumbWindowHeight}`)
      .style("font", "12px sans-serif")
      .style("margin", "5px");

    svg
      .append("rect")
      .attr("width", width)
      .attr("height", breadcrumbWindowHeight)
      .attr("fill", "none");
  
    const g = svg
      .selectAll("g")
      .data(icicle.sequence)
      .join("g")
      .attr("transform", (d, i) => `translate(${(i * breadcrumbWidth) % (9 * breadcrumbWidth)}, ${parseInt(i / 9) * breadcrumbHeight})`);
  
    g.append("polygon")
      .attr("points", breadcrumbPoints)
      .attr("fill", d => getColor(d.data.name))
      .attr("stroke", "white");
  
    g.append("text")
      .attr("x", (breadcrumbWidth + 10) / 2)
      .attr("y", 15)
      .attr("dy", "0.35em")
      .attr("text-anchor", "middle")
      .attr("fill", "white")
      .text(d => d.data.name);
  
    svg
      .append("text")
      .text(icicle.percentage > 0 ? icicle.percentage + "%" : "")
      .attr("x", 50)
      .attr("y", breadcrumbWindowHeight - 5)
  
    svg
      .append("text")
      .text(icicle.varValue)
      .attr("x", 0)
      .attr("y", breadcrumbWindowHeight - 5);
  
    return svg.node();
}

// Draws the icicle graph and handles mouseover events
function drawIcicles(container)
{
    d3.select(container).selectAll("*").remove();

    const root = partition(data);
    const svg = d3.select(container).append("svg");
    // Make this into a view, so that the currently hovered sequence is available to the breadcrumb
    const element = svg.node();
    element.value = { sequence: [], percentage: 0.0, varValue: "" };
  
    svg
      .attr("viewBox", `0 0 ${width} ${height}`)
      .style("font", "12px sans-serif");
  
    svg
      .append("rect")
      .attr("width", width)
      .attr("height", height)
      .attr("fill", "none");
  
    const segment = svg
      .append("g")
      .attr("transform", d =>
        `translate(0, ${-root.y1})`
      )
      .selectAll("rect")
      .data(
        root.descendants().filter(d => {
          // Don't draw the root node, and for efficiency, filter out nodes that would be too small to see
          return d.depth && segmentWidth(d) >= 0.1;
        })
      )
      .join("rect")
      .attr("fill", d => getColor(d.data.name))
      .attr("x", segmentX)
      .attr("y", segmentY)
      .attr("width", segmentWidth)
      .attr("height", segmentHeight)
      .on("mouseenter", (event, d) => {
        // Get the ancestors of the current segment, minus the root
        const sequence = d
          .ancestors()
          .reverse()
          .slice(1);
        // Highlight the ancestors
        segment.attr("fill-opacity", node =>
          sequence.indexOf(node) >= 0 ? 1.0 : 0.3
        );
        const percentage = ((100 * d.value) / root.value).toPrecision(3);
        const varValue = d.data.varValue;
        // Update the value of this view with the currently hovered sequence and percentage
        element.value = { sequence, percentage, varValue };
        icicle = element.value;
        element.dispatchEvent(new CustomEvent("input"));
        drawBreadcrumbs(".breadcrumb-container");
      });
  
    svg.on("mouseleave", () => {
      segment.attr("fill-opacity", 1);
      // Update the value of this view
      element.value = { sequence: [], percentage: 0.0, varValue: "" };
      icicle = element.value;
      element.dispatchEvent(new CustomEvent("input"));
      drawBreadcrumbs(".breadcrumb-container");
    });
}
