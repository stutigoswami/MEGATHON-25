# 🖋️ INKDOCK

> A fully-featured, real-time collaborative text editor with multi-user support, live synchronization, and an intuitive modern interface.

![Version](https://img.shields.io/badge/version-1.0.0-blue.svg)
![License](https://img.shields.io/badge/license-MIT-green.svg)
![C Backend](https://img.shields.io/badge/backend-C-00599C.svg)
![JavaScript](https://img.shields.io/badge/frontend-JavaScript-F7DF1E.svg)

## 📖 Overview

INKDOCK is a powerful collaborative text editor that enables multiple users to edit documents simultaneously in real-time. Built with a lightweight C backend (Mongoose 6.18) and a rich JavaScript frontend, it provides seamless collaboration with advanced features like live cursors, selection highlights, integrated chat, and rich text formatting.

## ✨ Features

### 🔐 User Authentication
- Secure session management with automatic expiry
- Quick access with demo accounts
- Seamless login/logout without disrupting active edits
- Session persistence across reconnections

### ✍️ Real-Time Collaborative Editing
- **Live synchronization** across all connected users
- **Conflict-free editing** with robust backend coordination
- **Rich text formatting**: Bold, italic, underline, font sizes, and families
- **Color customization** with integrated color picker
- Instant propagation of changes to all participants

### 👥 Multiple Cursors & Selection Highlights
- **Unique colored cursors** for each active user
- **Real-time selection highlights** showing what others are editing
- **Dynamic updates** that adapt to text changes
- Visual feedback for enhanced collaboration awareness

### 💬 Integrated Chat Panel
- **Sidebar chat** for instant messaging between collaborators
- **Timestamped messages** with preserved formatting
- **Real-time updates** without interfering with document editing
- Persistent chat history during session

### ⚠️ Merge Conflict Awareness
- Automatic detection of simultaneous edits in overlapping regions
- Visual conflict highlighting for manual resolution
- Ensures smooth collaboration even with multiple editors

### 🎨 Modern & Intuitive UI
- Clean, responsive layout compatible with various screen sizes
- Sidebar panel displaying active users and chat
- Comprehensive formatting toolbar
- Clear connection status indicators
- Elegant styling with user-friendly feedback

## 🚀 Getting Started

### Prerequisites
- GCC compiler
- POSIX-compliant system (Linux, macOS, WSL on Windows)
- Modern web browser with WebSocket support

### Installation

1. **Clone the repository**
   ```bash
   git clone https://github.com/stutigoswami/MEGATHON-25.git
   cd MEGATHON-25
   ```

2. **Compile the backend**
   ```bash
   gcc main.c mongoose.c -o server -lpthread
   ```
   This generates the `server` executable.

3. **Start the server**
   ```bash
   ./server
   ```
   The server will start on `http://localhost:8000` with WebSocket support enabled.

4. **Access the application**
   Open your browser and navigate to:
   ```
   http://localhost:8000/index.html
   ```

5. **Login with demo credentials**
   Use any of the following accounts:
   
   | Username | Password |
   |----------|----------|
   | alice    | alice123 |
   | bob      | bob123   |
   | charlie  | charlie123 |

### Testing Collaboration

To experience the full collaborative features:

1. Open multiple browser windows or tabs
2. Login with different user accounts in each window
3. Start editing - you'll see other users' cursors and selections in real-time
4. Use the chat panel to communicate with other users
5. Try simultaneous editing to see conflict detection in action

## 📁 Project Structure

```
MEGATHON-25/
├── main.c          # C backend server with authentication and WebSocket
├── mongoose.c      # Mongoose library implementation
├── mongoose.h      # Mongoose header files
├── index.html      # Frontend UI with editor and chat
├── server          # Compiled executable (generated after build)
└── README.md       # Project documentation
```

## 🛠️ Technical Details

### Backend Architecture
- **Language**: C
- **Framework**: Mongoose 6.18
- **Features**: HTTP server, WebSocket handling, session management
- **Concurrency**: Multi-threaded with pthread support

### Frontend Stack
- **Core**: Vanilla JavaScript
- **Communication**: WebSocket API for real-time updates
- **UI**: HTML5 + CSS3 with responsive design
- **Features**: Rich text editing, cursor synchronization, chat interface

### Communication Protocol
- WebSocket-based bidirectional communication
- JSON message format for text, cursors, and chat
- Automatic reconnection handling
- Efficient delta updates for text synchronization

## 🔧 Troubleshooting

| Issue | Solution |
|-------|----------|
| **Invalid credentials** | Ensure you're using one of the demo accounts: alice, bob, or charlie |
| **Cursors not appearing** | Verify multiple browser sessions are connected with different user accounts |
| **Chat not updating** | Check WebSocket connection status (green indicator in UI) |
| **Compilation errors** | Ensure GCC and pthread library are properly installed |
| **Port already in use** | Check if another instance is running or modify the port in `main.c` |

## 🤝 Contributing

Contributions are welcome! Please feel free to submit issues or pull requests.

1. Fork the repository
2. Create your feature branch (`git checkout -b feature/AmazingFeature`)
3. Commit your changes (`git commit -m 'Add some AmazingFeature'`)
4. Push to the branch (`git push origin feature/AmazingFeature`)
5. Open a Pull Request

## 📝 Future Enhancements

- [ ] User registration and dynamic authentication
- [ ] Document persistence and version history
- [ ] File export (PDF, DOCX, TXT)
- [ ] Advanced conflict resolution strategies
- [ ] User presence indicators (typing, idle, away)
- [ ] Collaborative drawing/diagram support
- [ ] Mobile app version

## 📄 License

This project is licensed under the MIT License - see the LICENSE file for details.

[https://drive.google.com/file/d/1qaNaTPur6n2ZkdCTD0L_fZSvz9MBEVVF/view?usp=drivesdk](https://drive.google.com/drive/folders/1LOOfHXFFdaCWstLYn9Grgu0x-V4-mJjR?usp=drive_link)

## 👥 Authors

Built with ❤️ by the TLExTTYL team

## 🙏 Acknowledgments

- **Mongoose** - Embedded web server library
- Inspired by modern collaborative editors like Google Docs and Notion
