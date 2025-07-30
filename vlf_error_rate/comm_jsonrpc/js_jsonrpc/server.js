// Module
const net = require("net");
const { JSONRPCClient } = require("json-rpc-2.0");

// Configuration
const HOST = "192.168.1.63";
const PORT = 61001;

// JSON-RPC Client Creation
/**
 * @param {net.Socket} socket
 * @returns {JSONRPCClient}
 */
function createJsonRpcClient(socket) {
    return new JSONRPCClient((jsonRpcRequest) => {
        return new Promise((resolve, reject) => {
            if (socket.writable) {
                socket.write(JSON.stringify(jsonRpcRequest) + "\n", "utf-8", (err) => {
                    if (err) return reject(err);
                    resolve();
                });
            } else {
                reject(new Error("Socket is not writable"));
            }
        });
    });
}

// Select Communication Type
function selectCommunicationType(index) {
    return index === 0 ? "receive" : "send";
}

// Send "set_condition" Request
async function setCondition(rpcClient) {
    console.log("Sending 'set_condition' request...");
    return rpcClient.request("set_condition", {
        data_type: 2,
        payload_size: 32,
        signal_len: 400,
    });
}

// Send "send" or "receive" Request
async function runCommunication(rpcClient, method) {
    console.log("Sending '" + method + "' request...");
    return rpcClient.request(method);
}

// Send "report" Request
async function report(rpcClient) {
    console.log("Sending 'report' request...");
    return rpcClient.request("report");
}

// Main Function to Handle Client Connection
async function handleClientConnection(socket, clientIndex) {
    const clientAddress = `${socket.remoteAddress}:${socket.remotePort}`;
    console.log(`\n--- New connection from ${clientAddress} ---`);

    // 送信
    const rpcClient = createJsonRpcClient(socket);

    // 受信
    let buffer = "";
    socket.on("data", (data) => {
        buffer += data.toString("utf-8");

        let newlineIndex;
        while ((newlineIndex = buffer.indexOf("\n")) !== -1) {
            const line = buffer.slice(0, newlineIndex);
            buffer = buffer.slice(newlineIndex + 1);

            try {
                const message = JSON.parse(line);
                rpcClient.receive(message);
            } catch (err) {
                console.error("Failed to parse message:", err.message);
                console.error("Raw message:", line);
            }
        }
    });

    socket.on("error", (err) => {
        console.error(`Socket Error from ${clientAddress}: ${err.message}`);
    });

    // Select Communication Type
    // 0: receive, 1: send
    // TODO: 今は0となっているが，接続順に応じて決定できるようにする
    const communicationMethod = selectCommunicationType(clientIndex % 2);
    const totalIterations = 1;

    try {
        // Send 'set_condition' Request
        try {
            const setResult = await setCondition(rpcClient);
            console.log(`[${clientAddress}] 'set_condition' request sent successfully:`, setResult);
        } catch (error) {
            console.error(`[${clientAddress}] 'set_condition' request failed:`, error.message);
            throw error;
        }

        for (let i = 0; i < totalIterations; i++) {
            console.log(`[${clientAddress}] Iteration ${i + 1}/${totalIterations} for '${communicationMethod}' communication...`);

            // Send 'send' or 'receive' Request
            try {
                const runCommResult = await runCommunication(rpcClient, communicationMethod);
                console.log(`[${clientAddress}] 'run_communication' request sent successfully:`, runCommResult);
            } catch (error) {
                console.error(`[${clientAddress}] 'run_communication' request failed:`, error.message);
                throw error;
            }
        

            // Send 'report' Request
            try {
                const reportResult = await report(rpcClient);
                console.log(`[${clientAddress}] 'report' request sent successfully:`, reportResult);
                
                // データを役割別に保存（reportのレスポンスに含まれるroleを使用）
                if (reportResult && reportResult.data && reportResult.role) {
                    roleData[reportResult.role] = reportResult.data;
                    console.log(`[${clientAddress}] Data saved for '${reportResult.role}' role`);
                }
            } catch (error) {
                console.error(`[${clientAddress}] 'report' request failed:`, error.message);
                throw error;
            }
        }

        console.log(`[${clientAddress}] All steps completed successfully.`);
        
        // 両方のデータが揃ったかチェック
        if (roleData.receive && roleData.send) {
            // データを比較する処理をここに追加可能
            if (roleData.receive === roleData.send) {
                console.log("Data transmission successful - data matches!");
            } else {
                console.log("Data transmission failed - data does not match");
            }
            
            // データをクリア（次回のテスト用）
            roleData.receive = null;
            roleData.send = null;
        }
        
        // for文が終了したらソケットを閉じて処理を終了
        console.log(`[${clientAddress}] Closing connection after completing all iterations.`);
        socket.end();
        return;
    } catch (error) {
        console.error(`[${clientAddress}] An error occurred during the session:`, error.message);
    } finally {
        console.log(`\n--- Connection from ${clientAddress} closed ---`);
        socket.end();
    }
}


// ソケット管理用
const roles = {
    receive: null, // 受信側のソケット
    send: null,    // 送信側のソケット
};

// データ保存用
const roleData = {
    receive: null, // 受信側のデータ
    send: null,    // 送信側のデータ
};

const activeSockets = new Set();


// Server Creation
const server = net.createServer((socket) => {
    let assignedRole = null;

    if (!roles.receive) {
        assignedRole = "receive";
        roles.receive = socket;
        console.log("Assigned role: receive");
        handleClientConnection(socket, 0);
    } else if (!roles.send) {
        assignedRole = "send";
        roles.send = socket;
        console.log("Assigned role: send");
        handleClientConnection(socket, 1);
    } else {
        console.log("Both roles are already assigned. Closing new connection.");
        socket.end("Both roles are already assigned. Please try again later.\n");
        return;
    }

    socket.on("close", () => {
        console.log(`Connection closed for role: ${assignedRole}`);
        if (assignedRole) {
            roles[assignedRole] = null;
            roleData[assignedRole] = null; // データもクリア
            console.log(`Slot ${assignedRole} is now free.`);
        }

        activeSockets.delete(socket);
    });

    socket.on("error", (err) => {
        console.error(`Socket Error for role ${assignedRole}:`, err.message);
        if (assignedRole) {
            roles[assignedRole] = null;
            roleData[assignedRole] = null; // データもクリア
            console.log(`Slot ${assignedRole} is now free due to error.`);
        }
    });
});


// --- Start Listening ---
server.listen(PORT, HOST, () => {
    const address = server.address();
    console.log(`Server is listening on ${address.address}:${address.port}`);
    console.log("Waiting for connections...");
});

// Shutdown
process.on("SIGINT", () => {
    console.log("\nSIGINT received. Shutting down server...");
    for (const socket of activeSockets) {
        socket.destroy();
    }
    server.close(() => {
        console.log("Server has been shut down.");
        process.exit(0);
    });
});
