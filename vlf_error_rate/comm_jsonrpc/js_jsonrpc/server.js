// Module
const net = require("net");
const { JSONRPCClient, JSONRPCServer } = require("json-rpc-2.0");

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

// JSON-RPC Server Creation
function createJsonRpcServer() {
    const server = new JSONRPCServer();
    
    // ESP32からのprepareReport通知を処理
    server.addMethod("prepareReport", (params) => {
        console.log("Received prepareReport notification:", params);
        
        // roleベースで待機中のPromiseを解決
        if (pendingNotifications.has(params.role)) {
            const { resolve } = pendingNotifications.get(params.role);
            pendingNotifications.delete(params.role);
            resolve(params);
        } else {
            console.warn(`No pending notification found for role: ${params.role}`);
        }
        
        // Notification（戻り値なし）
        return undefined;
    });
    
    return server;
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
    return rpcClient.request(method, {
        callback: "prepareReport"
    });
}

// Receive "prepareReport" Notification
async function waitForPrepareReportNotification(role) {
    return new Promise((resolve, reject) => {
        // roleをキーとして待機中のPromiseを保存
        pendingNotifications.set(role, { resolve, reject });
    });
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

    // 送信用のJSON-RPCクライアント
    const rpcClient = createJsonRpcClient(socket);
    
    // 受信用のJSON-RPCサーバー
    const rpcServer = createJsonRpcServer();


    // 受信時のイベントリスナー
    // ソケットにデータが到着したときに実行される
    let buffer = "";
    socket.on("data", (data) => {
        buffer += data.toString("utf-8");

        let newlineIndex;
        while ((newlineIndex = buffer.indexOf("\n")) !== -1) {
            const line = buffer.slice(0, newlineIndex);
            buffer = buffer.slice(newlineIndex + 1);

            try {
                const message = JSON.parse(line);
                                
                // JSON-RPC リクエスト/通知（サーバー側）
                if (message.method) {
                    rpcServer.receive(message).then((response) => {
                        // リクエストの場合はレスポンスを送信
                        if (response && socket.writable) {
                            socket.write(JSON.stringify(response) + "\n", "utf-8");
                        }
                        // 通知の場合はレスポンスなし
                    }).catch((error) => {
                        console.error("JSON-RPC Server error:", error);
                    });
                } else if (!message.method) {
                    // JSON-RPC レスポンス（クライアント側）
                    rpcClient.receive(message);
                }
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
    const communicationMethod = selectCommunicationType(clientIndex % 2);

    try {
        // Step 1:'set_condition' Request
        try {
            const setResult = await setCondition(rpcClient);
            console.log(`[${clientAddress}] 'set_condition' request sent successfully:`, setResult);
        } catch (error) {
            console.error(`[${clientAddress}] 'set_condition' request failed:`, error.message);
            throw error;
        }

        
        for (let i = 0; i < totalTransmissions; i++) {
            console.log(`[${clientAddress}] Iteration ${i + 1}/${totalTransmissions} for '${communicationMethod}' communication...`);


            // Step 2: 'send' or 'receive' Request
            try {
                const runCommResult = await runCommunication(rpcClient, communicationMethod);
                console.log(`[${clientAddress}] '${communicationMethod}' request sent successfully:`, runCommResult);
            } catch (error) {
                console.error(`[${clientAddress}] '${communicationMethod}' request failed:`, error.message);
                throw error;
            }

            // Step 3: Wait for 'prepareReport' notification
            try {
                console.log(`[${clientAddress}] Waiting for 'prepareReport' notification from ESP32...`);
                const notificationResult = await waitForPrepareReportNotification(communicationMethod);
                console.log(`[${clientAddress}] 'prepareReport' notification received:`, notificationResult);
            } catch (error) {
                console.error(`[${clientAddress}] Failed to receive 'prepareReport' notification:`, error.message);
                throw error;
            }

            // Step 4: 'report' Request
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

            clientsReady[communicationMethod] = true; 
            while (!clientsReady.receive || !clientsReady.send) {
                await new Promise(resolve => setTimeout(resolve, 100)); // 100ms待機
            }
            console.log(`[${clientAddress}] Report status: receive=${clientsReady.receive}, send=${clientsReady.send}`);


            // Step 5: データ比較（両方の報告が完了してから）

            console.log(`[${clientAddress}] Checking data comparison: receive=${!!roleData.receive}, send=${!!roleData.send}`);

            if (roleData.receive && roleData.send) {                
                // データを比較
                if (roleData.receive === roleData.send) {
                    successfulTransmissions++;
                    console.log(`[Iteration ${i + 1}] Data transmission successful - data matches!`);
                } else {
                    console.log(`[Iteration ${i + 1}] Data transmission failed - data does not match`);
                }
                
                // 比較完了後、両方のデータをクリア
                roleData.receive = null;
                roleData.send = null;
            }

                        // 両方のクライアントが報告完了するまで待機
            clientsReady[communicationMethod] = false; 
            while (clientsReady.receive || clientsReady.send) {
                await new Promise(resolve => setTimeout(resolve, 100)); // 100ms待機
            }
            console.log(`[${clientAddress}] Report status: receive=${clientsReady.receive}, send=${clientsReady.send}`);

        }

        console.log(`[${clientAddress}] All steps completed successfully.`);
        
        // 両方のクライアントが完了したかチェック
        if (!roles.receive && !roles.send) {
            // エラーレート計算
            const errorRate = ((totalTransmissions - successfulTransmissions) / totalTransmissions) * 100;
            const successRate = (successfulTransmissions / totalTransmissions) * 100;
            
            console.log("\n" + "=".repeat(50));
            console.log("FINAL TRANSMISSION STATISTICS");
            console.log("=".repeat(50));
            console.log(`Total transmissions: ${totalTransmissions}`);
            console.log(`Successful transmissions: ${successfulTransmissions}`);
            console.log(`Failed transmissions: ${totalTransmissions - successfulTransmissions}`);
            console.log(`Success rate: ${successRate.toFixed(2)}%`);
            console.log(`Error rate: ${errorRate.toFixed(2)}%`);
            console.log("=".repeat(50));
            
            // カウンターをリセット（次回のテスト用）
            totalTransmissions = 0;
            successfulTransmissions = 0;
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

// 通知待機用
const pendingNotifications = new Map(); // role -> { resolve, reject }

// 準備完了フラグ
const clientsReady = {
    receive: false,
    send: false
};

// エラーレート計測用
let totalTransmissions = 3;  // 総通信回数
let successfulTransmissions = 0;  // 成功した通信回数

// アクティブソケット
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
            clientsReady[assignedRole] = false; // 準備状態もクリア
            console.log(`Slot ${assignedRole} is now free.`);
        }

        activeSockets.delete(socket);
    });

    socket.on("error", (err) => {
        console.error(`Socket Error for role ${assignedRole}:`, err.message);
        if (assignedRole) {
            roles[assignedRole] = null;
            roleData[assignedRole] = null; // データもクリア
            clientsReady[assignedRole] = false; // 準備状態もクリア
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
