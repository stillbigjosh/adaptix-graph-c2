package main

import (
	"bytes"
	"crypto/hmac"
	"crypto/rand"
	"crypto/sha256"
	"encoding/base64"
	"encoding/hex"
	"encoding/json"
	"fmt"
	"net/url"
	"strings"
	"time"

	adaptix "github.com/Adaptix-Framework/axc2"
)

type Teamserver interface {
	TsAgentIsExists(agentId string) bool
	TsAgentCreate(agentCrc string, agentId string, beat []byte, listenerName string, ExternalIP string, Async bool) (adaptix.AgentData, error)
	TsAgentProcessData(agentId string, bodyData []byte) error
	TsAgentSetTick(agentId string, listenerName string) error
	TsAgentGetHostedAll(agentId string, maxDataSize int) ([]byte, error)
}

type PluginListener struct{}

var (
	ModuleDir       string
	ListenerDataDir string
	Ts              Teamserver
)

func InitPlugin(ts any, moduleDir string, listenerDir string) adaptix.PluginListener {
	ModuleDir = moduleDir
	ListenerDataDir = listenerDir
	Ts = ts.(Teamserver)
	return &PluginListener{}
}

type TransportConfig struct {
	StorageType    string `json:"storage_type"`
	TenantID       string `json:"tenant_id"`
	ClientID       string `json:"client_id"`
	ClientSecret   string `json:"client_secret"`
	UserID         string `json:"user_id"`
	FolderPath     string `json:"folder_path"`
	StorageAccount string `json:"storage_account"`
	StorageKey     string `json:"storage_key"`
	ContainerName  string `json:"container_name"`
	SasToken       string `json:"sas_token"`
	PollInterval   int    `json:"poll_interval"`
	EncryptKey     string `json:"encrypt_key"`
	Protocol       string `json:"protocol"`
	PollAttempts   int    `json:"poll_attempts"`
	PollIntervalMs int    `json:"poll_interval_ms"`
}

type Listener struct {
	name      string
	config    TransportConfig
	transport *TransportGraph
	active    bool
}

func generateEncryptKey() string {
	key := make([]byte, 16)
	rand.Read(key)
	return hex.EncodeToString(key)
}

func generateContainerSAS(accountName, accountKey, containerName string, expiry time.Time) (string, error) {
	key, err := base64.StdEncoding.DecodeString(accountKey)
	if err != nil {
		return "", fmt.Errorf("invalid storage key: %w", err)
	}

	start := time.Now().UTC().Add(-5 * time.Minute).Format("2006-01-02T15:04:05Z")
	expiryStr := expiry.UTC().Format("2006-01-02T15:04:05Z")
	version := "2020-10-02"
	permissions := "rwdl"
	canonicalResource := fmt.Sprintf("/blob/%s/%s", accountName, containerName)
	resource := "c"

	stringToSign := strings.Join([]string{
		permissions,
		start,
		expiryStr,
		canonicalResource,
		"", // identifier
		"", // IP
		"", // protocol
		version,
		resource,
		"", // snapshot
		"", // rscc
		"", // rscd
		"", // rsce
		"", // rscl
		"", // rsct
	}, "\n")

	mac := hmac.New(sha256.New, key)
	mac.Write([]byte(stringToSign))
	sig := base64.StdEncoding.EncodeToString(mac.Sum(nil))

	params := url.Values{}
	params.Set("sv", version)
	params.Set("sr", resource)
	params.Set("sp", permissions)
	params.Set("st", start)
	params.Set("se", expiryStr)
	params.Set("sig", sig)

	return params.Encode(), nil
}

func generateAccountSAS(accountName, accountKey string, expiry time.Time) (string, error) {
	key, err := base64.StdEncoding.DecodeString(accountKey)
	if err != nil {
		return "", fmt.Errorf("invalid storage key: %w", err)
	}

	start := time.Now().UTC().Add(-5 * time.Minute).Format("2006-01-02T15:04:05Z")
	expiryStr := expiry.UTC().Format("2006-01-02T15:04:05Z")
	version := "2020-10-02"
	permissions := "rwdlac"
	services := "b"
	resourceTypes := "sco"

	stringToSign := strings.Join([]string{
		accountName,
		permissions,
		services,
		resourceTypes,
		start,
		expiryStr,
		"", // IP
		"", // protocol
		version,
	}, "\n") + "\n"

	mac := hmac.New(sha256.New, key)
	mac.Write([]byte(stringToSign))
	sig := base64.StdEncoding.EncodeToString(mac.Sum(nil))

	params := url.Values{}
	params.Set("sv", version)
	params.Set("ss", services)
	params.Set("srt", resourceTypes)
	params.Set("sp", permissions)
	params.Set("st", start)
	params.Set("se", expiryStr)
	params.Set("sig", sig)

	return params.Encode(), nil
}

func (p *PluginListener) Create(name string, config string, customData []byte) (adaptix.ExtenderListener, adaptix.ListenerData, []byte, error) {
	var (
		listener     *Listener
		listenerData adaptix.ListenerData
		conf         TransportConfig
		customdData  []byte
		err          error
	)

	if customData == nil {
		if err = json.Unmarshal([]byte(config), &conf); err != nil {
			return nil, listenerData, customdData, err
		}
		conf.Protocol = "graph"
		if conf.StorageType == "" {
			conf.StorageType = "onedrive"
		}
		if conf.EncryptKey == "" {
			conf.EncryptKey = generateEncryptKey()
		}
		if conf.PollAttempts <= 0 {
			conf.PollAttempts = 15
		}
		if conf.PollIntervalMs <= 0 {
			conf.PollIntervalMs = 3000
		}
		if conf.PollInterval <= 0 {
			conf.PollInterval = 5
		}

		if conf.StorageType == "blob" && conf.SasToken == "" {
			if conf.StorageAccount == "" || conf.StorageKey == "" || conf.ContainerName == "" {
				return nil, listenerData, customdData, fmt.Errorf("blob mode requires storage_account, storage_key, and container_name")
			}
			conf.SasToken, err = generateContainerSAS(conf.StorageAccount, conf.StorageKey, conf.ContainerName, time.Now().Add(90*24*time.Hour))
			if err != nil {
				return nil, listenerData, customdData, fmt.Errorf("sas generation failed: %w", err)
			}
		}
	} else {
		if err = json.Unmarshal(customData, &conf); err != nil {
			return nil, listenerData, customdData, err
		}
	}

	transport := NewTransportGraph(name, conf)

	listener = &Listener{
		name:      name,
		config:    conf,
		transport: transport,
		active:    false,
	}

	bindHost := "graph-api"
	bindPort := conf.FolderPath
	if conf.StorageType == "blob" {
		bindHost = conf.StorageAccount + ".blob"
		bindPort = conf.ContainerName
	}

	agentAddr := "graph-api"
	if conf.StorageType == "blob" {
		agentAddr = conf.StorageAccount + ".blob.core.windows.net"
	}

	listenerData = adaptix.ListenerData{
		BindHost:  bindHost,
		BindPort:  bindPort,
		AgentAddr: agentAddr,
		Status:    "Stopped",
	}
	customdData, err = json.Marshal(conf)
	if err != nil {
		return nil, listenerData, customdData, err
	}

	return listener, listenerData, customdData, nil
}

func (l *Listener) Start() error {
	l.active = true
	return l.transport.Start()
}

func (l *Listener) Edit(config string) (adaptix.ListenerData, []byte, error) {
	var (
		listenerData adaptix.ListenerData
		conf         TransportConfig
		customdData  []byte
		err          error
	)

	if err = json.Unmarshal([]byte(config), &conf); err != nil {
		return listenerData, customdData, err
	}

	if l.config.StorageType == "onedrive" {
		l.config.UserID = conf.UserID
		l.config.FolderPath = conf.FolderPath
	} else {
		l.config.ContainerName = conf.ContainerName
	}
	l.config.PollInterval = conf.PollInterval
	if conf.PollAttempts > 0 {
		l.config.PollAttempts = conf.PollAttempts
	}
	if conf.PollIntervalMs > 0 {
		l.config.PollIntervalMs = conf.PollIntervalMs
	}
	l.transport.Config = l.config

	bindHost := "graph-api"
	bindPort := l.config.FolderPath
	if l.config.StorageType == "blob" {
		bindHost = l.config.StorageAccount + ".blob"
		bindPort = l.config.ContainerName
	}

	agentAddr := "graph-api"
	if l.config.StorageType == "blob" {
		agentAddr = l.config.StorageAccount + ".blob.core.windows.net"
	}

	listenerData = adaptix.ListenerData{
		BindHost:  bindHost,
		BindPort:  bindPort,
		AgentAddr: agentAddr,
	}
	if l.active {
		listenerData.Status = "Listen"
	} else {
		listenerData.Status = "Closed"
	}
	customdData, err = json.Marshal(l.config)

	return listenerData, customdData, err
}

func (l *Listener) Stop() error {
	l.active = false
	return l.transport.Stop()
}

func (l *Listener) GetProfile() ([]byte, error) {
	var buffer bytes.Buffer
	if err := json.NewEncoder(&buffer).Encode(l.config); err != nil {
		return nil, err
	}
	return buffer.Bytes(), nil
}

func (l *Listener) InternalHandler(data []byte) (string, error) {
	var agentId string
	_ = data
	return agentId, nil
}
