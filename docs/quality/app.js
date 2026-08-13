const labels = { build: "Build", tests: "Tests", consumerContract: "Consumer contract", coverage: "Coverage", staticAnalysis: "Static analysis", security: "Security" };
const formatTime = value => new Date(value).toLocaleString(undefined, { dateStyle: "medium", timeStyle: "short" });
const marker = value => value ? '<span class="yes">● yes</span>' : '<span class="no">—</span>';
const percent = value => Number(value).toFixed(1);

function render(documentData) {
  document.getElementById("overall").textContent = documentData.overall;
  document.getElementById("version").textContent = documentData.version;
  document.getElementById("commit").textContent = documentData.commit;
  document.getElementById("timestamp").textContent = formatTime(documentData.timestamp);
  document.getElementById("checks").innerHTML = Object.entries(documentData.checks).map(([key, status]) => `<div class="check ${status}"><span>${labels[key]}</span><strong>${status}</strong></div>`).join("");
  document.getElementById("tests").innerHTML = [["executables", "test executables"], ["passed", "passed"], ["failed", "failed"]].map(([key, label]) => `<div class="metric"><strong>${documentData.tests[key]}</strong><span>${label}</span></div>`).join("");
  document.getElementById("compiler").textContent = documentData.compiler;
  const coverage = documentData.coverage;
  document.getElementById("coverage").innerHTML = coverage.status === "available" ? `<div class="coverage-grid">${["lines", "functions", "regions", "branches"].map(key => `<div><strong>${percent(coverage[key].percent)}%</strong><span>${key}: ${coverage[key].covered}/${coverage[key].count}</span></div>`).join("")}</div>` : "<p>Coverage was not generated for this local dashboard build.</p>";
  document.getElementById("providers").innerHTML = documentData.providers.map(provider => `<tr><td>${provider.name}</td>${["get", "set", "edid", "dpcd", "mccs", "alternateInput"].map(key => `<td>${marker(provider.capabilities[key])}</td>`).join("")}</tr>`).join("");
}

fetch("quality.json").then(response => response.ok ? response.json() : Promise.reject(response.status)).then(render).catch(() => { document.getElementById("overall").textContent = "Unavailable"; });
