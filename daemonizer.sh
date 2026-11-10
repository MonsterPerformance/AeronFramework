sudo bash -c "cat << 'EOF' > /etc/systemd/system/aeron-driver.service
[Unit]
Description=Aeron Media Driver Daemon
After=network.target

[Service]
Type=simple
ExecStart=/usr/local/bin/aeron_driver
Restart=always
RestartSec=5
User=root
CPUAffinity=6

[Install]
WantedBy=multi-user.target
EOF"

# Reload systemd configuration changes
sudo systemctl daemon-reload

# Enable it to launch on system boot and start it right now
sudo systemctl enable --now aeron-driver.service
