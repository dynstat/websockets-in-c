# Import the asyncio library, which provides infrastructure for writing asynchronous code
import asyncio
# Import the websockets library, which implements the WebSocket protocol
import websockets

# Define an asynchronous function named 'echo' that handles WebSocket connections
# The 'async' keyword indicates that this function can be paused and resumed
async def echo(websocket):
    try:
        # Use an asynchronous for loop to iterate over incoming messages
        # This allows the server to handle multiple connections concurrently
        async for message in websocket:
            # Print the received message to the console
            print(f"Received: {message}")
            # Send the same message back to the client
            # The 'await' keyword is used to pause execution until the send operation is complete
            await websocket.send(message)
    except websockets.exceptions.ConnectionClosedError:
        # If the connection is closed unexpectedly, this exception is caught
        # We simply pass, effectively closing the connection gracefully
        pass

# Define an asynchronous function named 'main' that sets up the server
async def main():
    # Create a WebSocket server using the 'serve' function from the websockets library
    # It will use the 'echo' function to handle connections, listen on 'localhost' at port 8765
    server = await websockets.serve(echo, "localhost", 8765)
    # Wait for the server to close
    await server.wait_closed()

# Run the 'main' function using the 'run' function from the asyncio library
# This is a blocking call that runs the 'main' function and waits for it to complete
if __name__ == "__main__":
    asyncio.run(main())
