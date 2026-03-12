{{/*
Expand the name of the chart.
*/}}
{{- define "axosyslog.name" -}}
{{- default .Chart.Name .Values.nameOverride | trunc 63 | trimSuffix "-" }}
{{- end }}

{{/*
Create a default fully qualified app name.
We truncate at 63 chars because some Kubernetes name fields are limited to this (by the DNS naming spec).
If release name contains chart name it will be used as a full name.
*/}}
{{- define "axosyslog.fullname" -}}
{{- if .Values.fullnameOverride }}
{{- .Values.fullnameOverride | trunc 63 | trimSuffix "-" }}
{{- else }}
{{- $name := default .Chart.Name .Values.nameOverride }}
{{- if contains $name .Release.Name }}
{{- .Release.Name | trunc 63 | trimSuffix "-" }}
{{- else }}
{{- printf "%s-%s" .Release.Name $name | trunc 63 | trimSuffix "-" }}
{{- end }}
{{- end }}
{{- end }}

{{/*
Create chart name and version as used by the chart label.
*/}}
{{- define "axosyslog.chart" -}}
{{- printf "%s-%s" .Chart.Name .Chart.Version | replace "+" "_" | trunc 63 | trimSuffix "-" }}
{{- end }}

{{/*
Selector labels (immutable, used in spec.selector.matchLabels).
*/}}
{{- define "axosyslog.selectorLabels" -}}
app.kubernetes.io/name: {{ include "axosyslog.name" . }}
app.kubernetes.io/instance: {{ .Release.Name }}
{{- end }}

{{/*
Common labels (superset of selectorLabels, used on all metadata.labels).
*/}}
{{- define "axosyslog.labels" -}}
helm.sh/chart: {{ include "axosyslog.chart" . }}
{{ include "axosyslog.selectorLabels" . }}
{{- if .Chart.AppVersion }}
app.kubernetes.io/version: {{ .Chart.AppVersion | quote }}
{{- end }}
app.kubernetes.io/managed-by: {{ .Release.Service }}
{{- end }}

{{/*
Collector component selector labels.
*/}}
{{- define "axosyslog.collector.selectorLabels" -}}
{{ include "axosyslog.selectorLabels" . }}
app.kubernetes.io/component: collector
{{- end }}

{{/*
Collector component labels.
*/}}
{{- define "axosyslog.collector.labels" -}}
{{ include "axosyslog.labels" . }}
app.kubernetes.io/component: collector
{{- end }}

{{/*
Aggregator component selector labels.
*/}}
{{- define "axosyslog.aggregator.selectorLabels" -}}
{{ include "axosyslog.selectorLabels" . }}
app.kubernetes.io/component: aggregator
{{- end }}

{{/*
Aggregator component labels.
*/}}
{{- define "axosyslog.aggregator.labels" -}}
{{ include "axosyslog.labels" . }}
app.kubernetes.io/component: aggregator
{{- end }}

{{/*
Create the name of the service account to use
*/}}
{{- define "axosyslog.serviceAccountName" -}}
{{- if .Values.serviceAccount.create }}
{{- default (include "axosyslog.fullname" .) .Values.serviceAccount.name }}
{{- else }}
{{- default "default" .Values.serviceAccount.name }}
{{- end }}
{{- end }}

{{- define "axosyslog.namespace" -}}
{{ .Values.namespace | default .Release.Namespace }}
{{- end -}}
