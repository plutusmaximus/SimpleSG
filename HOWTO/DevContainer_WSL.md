1. In VS Code on Windows, install both extensions:
   - WSL
   - Dev Containers

2. Open WSL, go to the repo root:
   cd /home/kbaca/Dev/SimpleSG

3. Start VS Code from WSL:
   code .

4. Confirm VS Code is in WSL mode:
   bottom-left corner should say "WSL: <distro>"

5. Make sure the repo has:
   .devcontainer/devcontainer.json

6. Run:
   Ctrl+Shift+P
   Dev Containers: Reopen in Container

7. If Docker permission fails, run this in WSL, then restart WSL:
   sudo usermod -aG docker "$USER"
   wsl --shutdown