/// BeaconGraph

function ListenerUI(mode_create)
{
    let labelStorageType = form.create_label("Storage Backend:");
    let comboStorageType = form.create_combo();
    comboStorageType.addItems(["onedrive", "blob"]);
    comboStorageType.setEnabled(mode_create);

    let labelTenant = form.create_label("Tenant ID (OneDrive):");
    let editTenant = form.create_textline();
    editTenant.setPlaceholder("xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx");
    editTenant.setEnabled(mode_create);

    let labelClient = form.create_label("Client ID (OneDrive):");
    let editClient = form.create_textline();
    editClient.setPlaceholder("xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx");
    editClient.setEnabled(mode_create);

    let labelSecret = form.create_label("Client Secret (OneDrive):");
    let editSecret = form.create_textline();
    editSecret.setPlaceholder("Azure AD app client secret");
    editSecret.setEnabled(mode_create);

    let labelUser = form.create_label("User ID / UPN (OneDrive):");
    let editUser = form.create_textline();
    editUser.setPlaceholder("user@domain.com or user object ID");

    let labelFolder = form.create_label("OneDrive Folder:");
    let editFolder = form.create_textline();
    editFolder.setPlaceholder("/adaptix/c2");
    editFolder.setText("/adaptix/c2");

    let labelAccount = form.create_label("Storage Account (Blob):");
    let editAccount = form.create_textline();
    editAccount.setPlaceholder("mystorageaccount");
    editAccount.setEnabled(mode_create);

    let labelKey = form.create_label("Storage Key (Blob):");
    let editKey = form.create_textline();
    editKey.setPlaceholder("base64-encoded storage account key");
    editKey.setEnabled(mode_create);

    let labelContainer = form.create_label("Container Name (Blob):");
    let editContainer = form.create_textline();
    editContainer.setPlaceholder("adaptix-c2");
    editContainer.setText("adaptix-c2");

    let labelPoll = form.create_label("Poll Interval (s):");
    let spinPoll = form.create_spin();
    spinPoll.setRange(1, 3600);
    spinPoll.setValue(5);

    let labelEncKey = form.create_label("Encrypt Key:");
    let editEncKey = form.create_textline();
    editEncKey.setPlaceholder("32-char hex key (auto-generated if empty)");

    let labelPollAttempts = form.create_label("Beacon Poll Attempts:");
    let spinPollAttempts = form.create_spin();
    spinPollAttempts.setRange(1, 120);
    spinPollAttempts.setValue(15);

    let labelPollMs = form.create_label("Beacon Poll Interval (ms):");
    let spinPollMs = form.create_spin();
    spinPollMs.setRange(500, 60000);
    spinPollMs.setValue(3000);

    let layout = form.create_gridlayout();
    layout.addWidget(labelStorageType,  0, 0, 1, 1);
    layout.addWidget(comboStorageType,  0, 1, 1, 1);
    layout.addWidget(labelTenant,       1, 0, 1, 1);
    layout.addWidget(editTenant,        1, 1, 1, 1);
    layout.addWidget(labelClient,       2, 0, 1, 1);
    layout.addWidget(editClient,        2, 1, 1, 1);
    layout.addWidget(labelSecret,       3, 0, 1, 1);
    layout.addWidget(editSecret,        3, 1, 1, 1);
    layout.addWidget(labelUser,         4, 0, 1, 1);
    layout.addWidget(editUser,          4, 1, 1, 1);
    layout.addWidget(labelFolder,       5, 0, 1, 1);
    layout.addWidget(editFolder,        5, 1, 1, 1);
    layout.addWidget(labelAccount,      6, 0, 1, 1);
    layout.addWidget(editAccount,       6, 1, 1, 1);
    layout.addWidget(labelKey,          7, 0, 1, 1);
    layout.addWidget(editKey,           7, 1, 1, 1);
    layout.addWidget(labelContainer,    8, 0, 1, 1);
    layout.addWidget(editContainer,     8, 1, 1, 1);
    layout.addWidget(labelPoll,         9, 0, 1, 1);
    layout.addWidget(spinPoll,          9, 1, 1, 1);
    layout.addWidget(labelEncKey,       10, 0, 1, 1);
    layout.addWidget(editEncKey,        10, 1, 1, 1);
    layout.addWidget(labelPollAttempts, 11, 0, 1, 1);
    layout.addWidget(spinPollAttempts,  11, 1, 1, 1);
    layout.addWidget(labelPollMs,       12, 0, 1, 1);
    layout.addWidget(spinPollMs,        12, 1, 1, 1);

    let container = form.create_container();
    container.put("storage_type",    comboStorageType);
    container.put("tenant_id",       editTenant);
    container.put("client_id",       editClient);
    container.put("client_secret",   editSecret);
    container.put("user_id",         editUser);
    container.put("folder_path",     editFolder);
    container.put("storage_account", editAccount);
    container.put("storage_key",     editKey);
    container.put("container_name",  editContainer);
    container.put("poll_interval",   spinPoll);
    container.put("encrypt_key",     editEncKey);
    container.put("poll_attempts",   spinPollAttempts);
    container.put("poll_interval_ms", spinPollMs);

    let panel = form.create_panel();
    panel.setLayout(layout);

    return {
        ui_panel: panel,
        ui_container: container,
        ui_height: 580,
        ui_width: 560
    }
}
