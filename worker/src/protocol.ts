export type BrowserEngine = 'chromium' | 'firefox' | 'webkit';
export type OutputFormat = 'png' | 'webp' | 'avif' | 'pdf';
export type CaptureMode = 'fullPage' | 'viewport' | 'element';

export interface Viewport {
  id: string;
  name: string;
  width: number;
  height: number;
  deviceScaleFactor: number;
  mobile: boolean;
  enabled: boolean;
}

export interface CaptureProfile {
  id: string;
  name: string;
  viewports: Viewport[];
  engines: BrowserEngine[];
  formats: OutputFormat[];
  captureMode: CaptureMode;
  elementSelector: string;
  initialDelay: number;
  scrollDelay: number;
  finalDelay: number;
  concurrency: number;
  navigationTimeoutSeconds: number;
  selectorTimeoutSeconds: number;
  maxScrollSeconds: number;
  maxPageHeight: number;
  blockPopups: boolean;
  stripWhitespace: boolean;
  blocklist: string[];
  hideSelectors: string[];
  waitForSelector: string;
  namingTemplate: string;
  collisionPolicy: 'version' | 'overwrite' | 'skip';
  webpQuality: number;
  avifQuality: number;
  pdfFormat: string;
  pdfLandscape: boolean;
  pdfMargin: string;
  comparisonEnabled: boolean;
  pixelThreshold: number;
  mismatchThreshold: number;
  comparisonIgnoreSelectors: string[];
}

export interface BaselineRecord {
  comparisonKey: string;
  artifactId: string;
  artifact: Artifact;
}

export interface CaptureJob {
  id: string;
  projectId: string;
  projectRoot: string;
  profileId: string;
  source: string;
  urls: string[];
  profile: CaptureProfile;
  baselines?: Record<string, BaselineRecord>;
  allowLocalhost?: boolean;
}

export interface Artifact {
  id: string;
  jobId: string;
  url: string;
  finalUrl?: string;
  engine: BrowserEngine;
  viewportId: string;
  viewportName: string;
  captureMode: CaptureMode;
  format: OutputFormat;
  relativePath: string;
  width: number;
  height: number;
  sha256: string;
  status: 'succeeded' | 'failed' | 'skipped';
  error?: string;
  createdAt: string;
}

export interface WorkerEvent {
  protocolVersion: 2;
  sequence: number;
  timestamp: string;
  type: string;
  jobId: string;
  [key: string]: unknown;
}
