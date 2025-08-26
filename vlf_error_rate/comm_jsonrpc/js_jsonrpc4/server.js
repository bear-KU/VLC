// Module
const net = require("net");
const { JSONRPCClient, JSONRPCServer } = require("json-rpc-2.0");

// Configuration
const HOST = "192.168.1.63";
const PORT = 61001;
const TOTAL_TRANSMISSIONS = 10; // 総通信回数を定数化

// コマンドライン引数からVLCパラメータを設定
const args = process.argv.slice(2);
const VLC_DATA_TYPE = args[0] ? parseInt(args[0]) : 2;
const VLC_PAYLOAD_SIZE = args[1] ? parseInt(args[1]) : 32;
const VLC_SIGNAL_LEN = args[2] ? parseInt(args[2]) : 400;

console.log(`VLC Parameters: data_type=${VLC_DATA_TYPE}, payload_size=${VLC_PAYLOAD_SIZE}, signal_len=${VLC_SIGNAL_LEN}`);

// 16進数から2進数への変換テーブル
const HEX_TO_BIN = {
    '0': '0000', '1': '0001', '2': '0010', '3': '0011',
    '4': '0100', '5': '0101', '6': '0110', '7': '0111',
    '8': '1000', '9': '1001', 'A': '1010', 'B': '1011',
    'C': '1100', 'D': '1101', 'E': '1110', 'F': '1111'
};

// =================================================================
// JSON-RPC Client/Server Setup Functions
// =================================================================

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

/**
 * @param {net.Socket} socket
 * @param {JSONRPCClient} rpcClient
 * @param {Map} pendingNotifications
 * @returns {void}
 */
function setupRpcCommunication(socket, rpcClient, pendingNotifications) {
    const rpcServer = new JSONRPCServer();

    rpcServer.addMethod("prepareReport", (params) => {
        // console.log("Received prepareReport notification:", params);
        
        if (pendingNotifications.has(params.role)) {
            pendingNotifications.get(params.role).resolve(params);
            pendingNotifications.delete(params.role);
        } else {
            console.warn(`No pending notification for role: ${params.role}`);
        }
        return undefined; // Notification has no response
    });

    let buffer = "";
    socket.on("data", (data) => {
        buffer += data.toString("utf-8");

        let newlineIndex;
        while ((newlineIndex = buffer.indexOf("\n")) !== -1) {
            const line = buffer.slice(0, newlineIndex);
            buffer = buffer.slice(newlineIndex + 1);

            try {
                const message = JSON.parse(line);
                if (message.method) {
                    rpcServer.receive(message);
                } else {
                    rpcClient.receive(message);
                }
            } catch (err) {
                console.error("Failed to parse message:", err.message, "Raw:", line);
            }
        }
    });
}

// ビット誤り率を計算する関数
function calculateBitErrors(sentHex, receivedHex) {
    // 送信データがない場合はエラーにできないので0を返す
    if (!sentHex) {
        return { errors: 0, totalBits: 0 };
    }
    const totalBits = sentHex.length * 4;

    // 受信失敗、または受信データがない場合は全ビットエラーとして扱う
    if (!receivedHex || receivedHex === 'ERROR') {
        return { errors: totalBits, totalBits: totalBits };
    }

    let errorCount = 0;
    // 比較は短い方の文字列長で行う
    const len = Math.min(sentHex.length, receivedHex.length);

    for (let i = 0; i < len; i++) {
        const sentChar = sentHex[i];
        const receivedChar = receivedHex[i];

        // 文字が異なれば、ビットレベルで比較
        if (sentChar !== receivedChar) {
            const sentBits = HEX_TO_BIN[sentChar];
            const receivedBits = HEX_TO_BIN[receivedChar];
            
            // 変換テーブルにない不正な文字が来た場合を考慮
            if (!sentBits || !receivedBits) {
                errorCount += 4; // 不正な文字は4ビットエラーとして扱う
                continue;
            }

            for (let j = 0; j < 4; j++) {
                if (sentBits[j] !== receivedBits[j]) {
                    errorCount++;
                }
            }
        }
    }
    
    // 送受信で文字列長が異なる場合、その差分はすべてエラーとしてカウント
    errorCount += Math.abs(sentHex.length - receivedHex.length) * 4;

    return { errors: errorCount, totalBits: totalBits };
}

// =================================================================
// Test Logic Functions (Central Commander Model)
// =================================================================

/**
 * 1回のイテレーションでクライアントが実行する処理
 * @param {JSONRPCClient} rpcClient
 * @param {string} role 'send' or 'receive'
 * @param {Map} pendingNotifications
 * @returns {Promise<object>}
 */
async function performClientIteration(rpcClient, role, pendingNotifications) {
    // 'send' or 'receive' request
    await rpcClient.request(role, { callback: "prepareReport" });

    // Wait for 'prepareReport' notification
    const notificationPromise = new Promise((resolve, reject) => {
        pendingNotifications.set(role, { resolve, reject });
        // Add a timeout to prevent hanging forever
        setTimeout(() => reject(new Error(`Timeout waiting for prepareReport from ${role}`)), 20000);
    });
    await notificationPromise;

    // 'report' request
    return rpcClient.request("report");
}

/**
 * 2台のクライアントが揃った後のテスト全体を管理する
 * @param {net.Socket} receiverSocket
 * @param {net.Socket} senderSocket
 */
async function manageTestRun(receiverSocket, senderSocket) {
    console.log("\n--- Starting new test run ---");
    // BER計算用の変数を初期化
    let totalBitErrors = 0;
    let totalBitsTransmitted = 0;

    const pendingNotifications = new Map();
    const receiverRpc = createJsonRpcClient(receiverSocket);
    const senderRpc = createJsonRpcClient(senderSocket);

    setupRpcCommunication(receiverSocket, receiverRpc, pendingNotifications);
    setupRpcCommunication(senderSocket, senderRpc, pendingNotifications);

    try {
        // Step 1: 両方のクライアントに 'set_condition' を送信
        console.log("Step 1: Setting conditions for both clients...");
        const conditionParams = { 
            data_type: VLC_DATA_TYPE, 
            payload_size: VLC_PAYLOAD_SIZE, 
            signal_len: VLC_SIGNAL_LEN 
        };
        const setReceiverPromise = receiverRpc.request("set_condition", conditionParams);
        const setSenderPromise = senderRpc.request("set_condition", conditionParams);
        await Promise.all([setReceiverPromise, setSenderPromise]);
        console.log("Conditions set successfully.");

        // Step 2: イテレーションを開始
        for (let i = 0; i < TOTAL_TRANSMISSIONS; i++) {
            console.log(`\n--- Iteration ${i + 1}/${TOTAL_TRANSMISSIONS} ---`);

            const receiverPromise = performClientIteration(receiverRpc, 'receive', pendingNotifications);
            const senderPromise = performClientIteration(senderRpc, 'send', pendingNotifications);

            // 両方のクライアントの処理が完了するのを待つ
            const [receiverResult, senderResult] = await Promise.all([receiverPromise, senderPromise]);

            console.log("Both clients reported back.");
            console.log("  Receiver data:", receiverResult.data ? receiverResult.data : 'N/A');
            console.log("  Sender data:  ", senderResult.data ? senderResult.data : 'N/A');

            // Step 3: ビット誤り率を計算
            const bitErrorStats = calculateBitErrors(senderResult.data, receiverResult.data);
            totalBitErrors += bitErrorStats.errors;
            totalBitsTransmitted += bitErrorStats.totalBits;

            if (bitErrorStats.errors === 0) {
                console.log("Result: SUCCESS - Data matches perfectly.");
            } else {
                console.log(`Result: FAILURE - Found ${bitErrorStats.errors} bit errors in this iteration.`);
            }
        }

    } catch (error) {
        console.error("\nAn error occurred during the test run:", error.message);
    } finally {
        // Step 4: 最終結果の表示と接続終了
        console.log("\n=============================================================");
        console.log("--- Test run finished ---");
        // ビット誤り率を計算
        const bitErrorRate = totalBitsTransmitted > 0 ? (totalBitErrors / totalBitsTransmitted) * 100 : 0;
        console.log(`Total bits transmitted: ${totalBitsTransmitted}`);
        console.log(`Total bit errors: ${totalBitErrors}`);
        console.log(`Bit Error Rate (BER): ${bitErrorRate.toFixed(6)/100.0}`);
        console.log("=============================================================\n");

        receiverSocket.end();
        senderSocket.end();
    }
}

// =================================================================
// Main Server Logic
// =================================================================

const clients = {
    receive: null,
    send: null,
};
const activeSockets = new Set();

const server = net.createServer((socket) => {
    activeSockets.add(socket);
    let assignedRole = null;

    if (!clients.receive) {
        assignedRole = "receive";
        clients.receive = socket;
        console.log(`Client connected. Assigned role: ${assignedRole}`);
    } else if (!clients.send) {
        assignedRole = "send";
        clients.send = socket;
        console.log(`Client connected. Assigned role: ${assignedRole}`);
    } else {
        console.log("Both roles are busy. Rejecting new connection.");
        socket.end("Server is busy. Please try again later.\n");
        return;
    }

    socket.on("close", () => {
        console.log(`Client [${assignedRole}] disconnected.`);
        if (assignedRole) {
            clients[assignedRole] = null;
        }
        activeSockets.delete(socket);
    });

    socket.on("error", (err) => {
        console.error(`Socket error from [${assignedRole}]: ${err.message}`);
    });

    // 両方のクライアントが揃ったら、テスト管理を開始
    if (clients.receive && clients.send) {
        console.log("Both clients are connected. Handing over to the test manager.");
        // manageTestRun に処理を移譲し、現在のクライアントペアをクリア
        const receiverSocket = clients.receive;
        const senderSocket = clients.send;
        clients.receive = null;
        clients.send = null;
        manageTestRun(receiverSocket, senderSocket);
    }
});

server.listen(PORT, HOST, () => {
    console.log(`Server is listening on ${HOST}:${PORT}`);
    console.log("Waiting for two clients ('receive' and 'send') to connect...");
});

process.on("SIGINT", () => {
    console.log("\nShutting down server.");

    for (const socket of activeSockets) {
        socket.destroy();
    }

    server.close(() => {
        process.exit(0);
    });
});
