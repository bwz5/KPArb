const http = require("http");
const fs = require("fs");
const path = require("path");
const { summary, setKillSwitch } = require("./metrics");

const HOST = process.env.KPARB_DASHBOARD_HOST || "127.0.0.1";
const PORT = Number(process.env.KPARB_DASHBOARD_PORT || 4173);
const PUBLIC_DIRECTORY = path.join(__dirname, "public");
const CONTENT_TYPES = { ".html": "text/html; charset=utf-8", ".js": "text/javascript; charset=utf-8", ".css": "text/css; charset=utf-8" };
const MAXIMUM_BODY_BYTES = 1024;

function sendJson(response, status, value) {
  response.writeHead(status, { "Content-Type": "application/json; charset=utf-8", "Cache-Control": "no-store" });
  response.end(JSON.stringify(value));
}

function serveStatic(request, response) {
  const requested = request.url === "/" ? "/index.html" : request.url.split("?")[0];
  const file = path.normalize(path.join(PUBLIC_DIRECTORY, requested));
  if (!file.startsWith(PUBLIC_DIRECTORY) || !fs.existsSync(file) || fs.statSync(file).isDirectory()) {
    response.writeHead(404);
    response.end("not found");
    return;
  }
  response.writeHead(200, { "Content-Type": CONTENT_TYPES[path.extname(file)] || "application/octet-stream" });
  fs.createReadStream(file).pipe(response);
}

const server = http.createServer(async (request, response) => {
  try {
    if (request.method === "GET" && request.url.startsWith("/api/summary")) {
      sendJson(response, 200, await summary());
      return;
    }
    if (request.method === "POST" && request.url === "/api/kill-switch") {
      let body = "";
      request.on("data", (chunk) => {
        body += chunk;
        if (body.length > MAXIMUM_BODY_BYTES) {
          request.destroy();
        }
      });
      request.on("end", () => {
        try {
          const { engaged } = JSON.parse(body || "{}");
          sendJson(response, 200, { engaged: setKillSwitch(Boolean(engaged)) });
        } catch (error) {
          sendJson(response, 400, { error: error.message });
        }
      });
      return;
    }
    if (request.method === "GET") {
      serveStatic(request, response);
      return;
    }
    response.writeHead(405);
    response.end();
  } catch (error) {
    sendJson(response, 500, { error: error.message });
  }
});

server.listen(PORT, HOST, () => {
  console.log(`KPArb dashboard on http://${HOST}:${PORT}`);
});
